# Creates the F1DockTimers release key, once, on the maintainer's PC:
#   powershell -ExecutionPolicy Bypass -File tools\new-release-key.ps1
#
# Asks for a password (twice, 12 or more characters), writes the encrypted private key to
# %USERPROFILE%\.release-keys\f1docktimers.key (outside the repository) and adds the public key to
# src\core\release_key.h. Commit that header. Back up the key file and keep the password: without
# them installed copies can no longer update themselves (docs\RELEASING.md).
param(
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.release-keys\f1docktimers.key'),
    [string]$Header = (Join-Path (Split-Path $PSScriptRoot -Parent) 'src\core\release_key.h'),
    [Security.SecureString]$Password   # for tests only; normally typed when asked
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-key.psm1') -Force

if (Test-Path -LiteralPath $KeyFile) { throw "$KeyFile already exists. Not overwriting a release key; pick another -KeyFile to make a new one." }
if (-not (Test-Path -LiteralPath $Header)) { throw "No $Header." }

if (-not $Password) {
    Write-Host 'Choose a password for the release key (12+ characters). It is needed for every release.'
    $Password = Read-Host -AsSecureString 'Password'
    $again = Read-Host -AsSecureString 'Same password again'
    $a = [Runtime.InteropServices.Marshal]::SecureStringToGlobalAllocUnicode($Password)
    $b = [Runtime.InteropServices.Marshal]::SecureStringToGlobalAllocUnicode($again)
    try {
        $same = $Password.Length -eq $again.Length
        for ($i = 0; $same -and $i -lt $Password.Length; $i++) {
            $same = [Runtime.InteropServices.Marshal]::ReadInt16($a, 2 * $i) -eq [Runtime.InteropServices.Marshal]::ReadInt16($b, 2 * $i)
        }
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeGlobalAllocUnicode($a)
        [Runtime.InteropServices.Marshal]::ZeroFreeGlobalAllocUnicode($b)
        $again.Dispose()
    }
    if (-not $same) { throw 'The passwords differ. Nothing was created.' }
}
if ($Password.Length -lt 12) { throw 'Use at least 12 characters. Nothing was created.' }

Write-Host 'Creating the key (deriving the encryption key takes a few seconds)...'
$key = New-ReleaseKey $Password
New-Item -ItemType Directory -Force (Split-Path $KeyFile -Parent) | Out-Null
# CreateNew: never replaces a file that appeared in the meantime.
$stream = [IO.File]::Open($KeyFile, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
try { $bytes = [Text.Encoding]::ASCII.GetBytes($key.File); $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Close() }

$text = Get-Content -LiteralPath $Header -Raw
if ($text -match $key.Public) { throw 'That public key is already in the header.' }
$line = "    `"$($key.Public)`",  // created $(Get-Date -Format yyyy-MM-dd)`r`n"
$text = $text -replace '(\r?\n)(\s*// keys:end)', "`$1$line`$2"
if ($text -notmatch $key.Public) { throw "Could not find the '// keys:end' marker in $Header." }
[IO.File]::WriteAllText($Header, $text.Replace("`r`n", "`n"), (New-Object Text.UTF8Encoding $false))

Write-Host ''
Write-Host "Key file:   $KeyFile"
Write-Host "Public key: $($key.Public)"
Write-Host "Added to:   $Header (commit it)"
Write-Host ''
Write-Host 'Now back up the key file somewhere safe (for example with the password in a password manager).'
Write-Host 'If the key file or the password is lost, installed copies cannot update themselves any more.'
