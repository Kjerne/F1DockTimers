# Signs a published release's checksum list with the release key, so installed copies may install it:
#   powershell -ExecutionPolicy Bypass -File tools\sign-release.ps1 -Version 1.1.1
#
# Downloads SHA256SUMS.txt from the GitHub release (CI makes it), shows it and whether it matches
# the files in dist\ (if you built the same version here), asks you to confirm, asks for the key
# password and writes SHA256SUMS.txt.sig. Then upload that file to the release: with -Upload the
# script does it through the GitHub CLI (gh), otherwise it opens the release page. Until the .sig is
# there the app shows the update but only links to the release page.
#
# Only sign a release you published yourself: the signature is what tells installed copies that
# these exact files are yours.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.release-keys\f1docktimers.key'),
    [string]$SumsFile,                   # sign a local SHA256SUMS.txt instead of the release's
    [string]$OutFile,                    # default: dist\SHA256SUMS.txt.sig
    [switch]$Upload,                     # upload the .sig with gh after signing
    [Security.SecureString]$Password,    # for tests only; normally typed when asked
    [switch]$Yes                         # for tests only: do not ask before signing
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-key.psm1') -Force
$repo = 'Kjerne/F1DockTimers'
$root = Split-Path $PSScriptRoot -Parent

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must look like 1.2.3.' }
if (-not (Test-Path -LiteralPath $KeyFile)) { throw "No key file at $KeyFile. Create it once with tools\new-release-key.ps1." }
$key = Read-ReleaseKeyFile $KeyFile
$headerKeys = @(Get-HeaderKeys (Join-Path $root 'src\core\release_key.h'))
if ($headerKeys -notcontains $key.public) {
    Write-Warning 'This key is not in src\core\release_key.h, so the app would not accept its signatures.'
}
if (-not $OutFile) { $OutFile = Join-Path $root 'dist\SHA256SUMS.txt.sig' }

if (-not $SumsFile) {
    $SumsFile = Join-Path ([IO.Path]::GetTempPath()) "F1DockTimers-$Version-SHA256SUMS.txt"
    $url = "https://github.com/$repo/releases/download/v$Version/SHA256SUMS.txt"
    Write-Host "Downloading $url"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest $url -OutFile $SumsFile -UseBasicParsing
}
$data = [IO.File]::ReadAllBytes($SumsFile)
$text = [Text.Encoding]::ASCII.GetString($data)
$setup = "F1DockTimers-$Version-setup.exe"
if (([regex]::Matches($text, "(?m)^[0-9a-fA-F]{64} [ *]$([regex]::Escape($setup))\r?$")).Count -ne 1) {
    throw "SHA256SUMS.txt must list $setup exactly once."
}

Write-Host ''
Write-Host "SHA256SUMS.txt for v${Version}:"
foreach ($line in $text -split "`n") {
    if ($line -match '^([0-9a-fA-F]{64}) [ *](.+?)\r?$') {
        $local = Join-Path $root "dist\$($Matches[2])"
        $note = ''
        if (Test-Path -LiteralPath $local) {
            $hash = (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash
            $note = if ($hash -eq $Matches[1].ToUpper()) { '   (same as dist\)' } else { '   (DIFFERENT from dist\)' }
        }
        Write-Host "  $($Matches[1])  $($Matches[2])$note"
    }
}
Write-Host ''
if (-not $Yes) {
    $answer = Read-Host "Sign this list as the F1DockTimers $Version release? Type yes"
    if ($answer -ne 'yes') { Write-Host 'Not signed.'; exit 1 }
}
if (-not $Password) { $Password = Read-Host -AsSecureString 'Release key password' }
$sig = Invoke-ReleaseSign $KeyFile $Password $data
New-Item -ItemType Directory -Force (Split-Path $OutFile -Parent) | Out-Null
[IO.File]::WriteAllBytes($OutFile, $sig)
Write-Host "Signed: $OutFile"

if ($Upload) {
    gh release upload "v$Version" $OutFile --repo $repo --clobber
    if ($LASTEXITCODE -ne 0) { throw 'Upload failed; upload the file on the release page by hand.' }
    Write-Host "Uploaded to https://github.com/$repo/releases/tag/v$Version"
} elseif (-not $Yes) {
    Write-Host 'Upload SHA256SUMS.txt.sig to the release (opening its edit page).'
    Start-Process "https://github.com/$repo/releases/edit/v$Version"
}
