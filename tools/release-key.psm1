# Helpers for the F1DockTimers release key (used by new-release-key.ps1 and sign-release.ps1).
#
# The key is ECDSA P-256. Its key file holds the public key in the clear and the private scalar
# encrypted with a password: PBKDF2-HMAC-SHA256 (600,000 rounds, random salt) gives an AES-256 key
# and an HMAC-SHA256 key; the scalar is AES-256-CBC encrypted, and the HMAC covers everything, so a
# wrong password or a changed file is detected before anything is signed. The password and the
# private key only exist in this process's memory and are cleared after use; nothing is logged.
Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'

$script:Format = 'f1docktimers-release-key-1'
$script:Rounds = 600000

function ConvertTo-Hex([byte[]]$bytes) { ($bytes | ForEach-Object { $_.ToString('x2') }) -join '' }
function ConvertFrom-Hex([string]$hex) {
    $out = New-Object byte[] ($hex.Length / 2)
    for ($i = 0; $i -lt $out.Length; $i++) { $out[$i] = [Convert]::ToByte($hex.Substring(2 * $i, 2), 16) }
    , $out
}

function Use-PasswordBytes([Security.SecureString]$Password, [scriptblock]$Body) {
    # Hands the password to $Body as UTF-8 bytes and wipes every copy afterwards.
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToGlobalAllocUnicode($Password)
    $chars = New-Object char[] $Password.Length
    try {
        [Runtime.InteropServices.Marshal]::Copy($ptr, $chars, 0, $chars.Length)
        $bytes = [Text.Encoding]::UTF8.GetBytes($chars)
        try { & $Body $bytes } finally { [Array]::Clear($bytes, 0, $bytes.Length) }
    } finally {
        [Array]::Clear($chars, 0, $chars.Length)
        [Runtime.InteropServices.Marshal]::ZeroFreeGlobalAllocUnicode($ptr)
    }
}

function Get-DerivedKeys([Security.SecureString]$Password, [byte[]]$Salt, [int]$Rounds) {
    Use-PasswordBytes $Password {
        param($pw)
        $kdf = New-Object Security.Cryptography.Rfc2898DeriveBytes($pw, $Salt, $Rounds, [Security.Cryptography.HashAlgorithmName]::SHA256)
        try { , $kdf.GetBytes(64) } finally { $kdf.Dispose() }
    }
}

function Get-Hmac([byte[]]$Key, [byte[][]]$Parts) {
    $h = New-Object Security.Cryptography.HMACSHA256 (, $Key)
    try {
        $all = New-Object IO.MemoryStream
        foreach ($p in $Parts) { $all.Write($p, 0, $p.Length) }
        , $h.ComputeHash($all.ToArray())
    } finally { $h.Dispose() }
}

function Test-Equal([byte[]]$a, [byte[]]$b) {
    # Constant-time compare.
    if ($a.Length -ne $b.Length) { return $false }
    $d = 0
    for ($i = 0; $i -lt $a.Length; $i++) { $d = $d -bor ($a[$i] -bxor $b[$i]) }
    $d -eq 0
}

function New-ReleaseKey([Security.SecureString]$Password) {
    # Returns @{ Public = 'hex128'; File = <key file text> }.
    $ec = [Security.Cryptography.ECDsa]::Create([Security.Cryptography.ECCurve+NamedCurves]::nistP256)
    try {
        $p = $ec.ExportParameters($true)
        $public = ConvertTo-Hex ([byte[]]($p.Q.X + $p.Q.Y))
        $salt = New-Object byte[] 16; $iv = New-Object byte[] 16
        $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
        $rng.GetBytes($salt); $rng.GetBytes($iv); $rng.Dispose()
        $keys = Get-DerivedKeys $Password $salt $script:Rounds
        try {
            $aes = [Security.Cryptography.Aes]::Create()
            $aes.Key = [byte[]]$keys[0..31]; $aes.IV = $iv
            $enc = $aes.CreateEncryptor()
            $ct = $enc.TransformFinalBlock($p.D, 0, $p.D.Length)
            $enc.Dispose(); $aes.Dispose()
            $mac = Get-Hmac ([byte[]]$keys[32..63]) @([Text.Encoding]::ASCII.GetBytes($script:Format + $public), $salt, $iv, $ct)
        } finally {
            [Array]::Clear($keys, 0, $keys.Length)
            [Array]::Clear($p.D, 0, $p.D.Length)
        }
        $file = [ordered]@{
            format = $script:Format; curve = 'P-256'; public = $public
            kdf = 'PBKDF2-HMAC-SHA256'; rounds = $script:Rounds
            salt = [Convert]::ToBase64String($salt); iv = [Convert]::ToBase64String($iv)
            privateKey = [Convert]::ToBase64String($ct); mac = [Convert]::ToBase64String($mac)
        } | ConvertTo-Json
        @{ Public = $public; File = $file }
    } finally { $ec.Dispose() }
}

function Read-ReleaseKeyFile([string]$Path) {
    $k = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($k.format -ne $script:Format -or $k.curve -ne 'P-256' -or $k.public -notmatch '^[0-9a-f]{128}$') {
        throw "$Path is not an F1DockTimers release key file."
    }
    $k
}

function Invoke-ReleaseSign([string]$KeyPath, [Security.SecureString]$Password, [byte[]]$Data) {
    # Signs $Data (ECDSA P-256 over SHA-256), returns the 64-byte r||s signature. Throws on a wrong
    # password or a changed key file.
    $k = Read-ReleaseKeyFile $KeyPath
    $salt = [Convert]::FromBase64String($k.salt); $iv = [Convert]::FromBase64String($k.iv)
    $ct = [Convert]::FromBase64String($k.privateKey)
    $keys = Get-DerivedKeys $Password $salt ([int]$k.rounds)
    $d = $null
    try {
        $mac = Get-Hmac ([byte[]]$keys[32..63]) @([Text.Encoding]::ASCII.GetBytes($k.format + $k.public), $salt, $iv, $ct)
        if (-not (Test-Equal $mac ([Convert]::FromBase64String($k.mac)))) { throw 'Wrong password, or the key file was changed.' }
        $aes = [Security.Cryptography.Aes]::Create()
        $aes.Key = [byte[]]$keys[0..31]; $aes.IV = $iv
        $dec = $aes.CreateDecryptor()
        $d = $dec.TransformFinalBlock($ct, 0, $ct.Length)
        $dec.Dispose(); $aes.Dispose()
        $pub = ConvertFrom-Hex $k.public
        $params = New-Object Security.Cryptography.ECParameters
        $params.Curve = [Security.Cryptography.ECCurve+NamedCurves]::nistP256
        $q = New-Object Security.Cryptography.ECPoint
        $q.X = [byte[]]$pub[0..31]; $q.Y = [byte[]]$pub[32..63]
        $params.Q = $q; $params.D = $d
        $ec = [Security.Cryptography.ECDsa]::Create($params)
        try { $sig = $ec.SignData($Data, [Security.Cryptography.HashAlgorithmName]::SHA256) } finally { $ec.Dispose() }
        if ($sig.Length -ne 64) { throw "Unexpected signature size $($sig.Length)." }
        if (-not (Test-ReleaseSignature $k.public $Data $sig)) { throw 'The new signature does not verify; nothing written.' }
        , $sig
    } finally {
        [Array]::Clear($keys, 0, $keys.Length)
        if ($d) { [Array]::Clear($d, 0, $d.Length) }
    }
}

function Test-ReleaseSignature([string]$PublicHex, [byte[]]$Data, [byte[]]$Signature) {
    $pub = ConvertFrom-Hex $PublicHex
    $params = New-Object Security.Cryptography.ECParameters
    $params.Curve = [Security.Cryptography.ECCurve+NamedCurves]::nistP256
    $q = New-Object Security.Cryptography.ECPoint
    $q.X = [byte[]]$pub[0..31]; $q.Y = [byte[]]$pub[32..63]
    $params.Q = $q
    $ec = [Security.Cryptography.ECDsa]::Create($params)
    try { $ec.VerifyData($Data, $Signature, [Security.Cryptography.HashAlgorithmName]::SHA256) } finally { $ec.Dispose() }
}

function Get-HeaderKeys([string]$HeaderPath) {
    [regex]::Matches((Get-Content -LiteralPath $HeaderPath -Raw), '"([0-9a-fA-F]{128})"') | ForEach-Object { $_.Groups[1].Value.ToLower() }
}

Export-ModuleMember -Function New-ReleaseKey, Read-ReleaseKeyFile, Invoke-ReleaseSign, Test-ReleaseSignature, Get-HeaderKeys
