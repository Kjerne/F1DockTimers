# Tests tools\new-release-key.ps1 and tools\sign-release.ps1 with a throwaway key in a temp folder
# (never the real key file or header), and refreshes the fixture tests\fixtures\release_sig\ that
# tst_updater uses to check the app accepts what these scripts sign.
#   powershell -ExecutionPolicy Bypass -File tools\test-release-key.ps1 [-WriteFixture]
param([switch]$WriteFixture)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("f1dt-keytest-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $tmp | Out-Null
$failures = 0
function Check($ok, $what) { if ($ok) { Write-Host "  ok    $what" } else { Write-Host "  FAIL  $what"; $script:failures++ } }
function Expect-Throw([scriptblock]$b, $what) { try { & $b | Out-Null; Check $false $what } catch { Check $true $what } }

try {
    $header = Join-Path $tmp 'release_key.h'
    Copy-Item (Join-Path $root 'src\core\release_key.h') $header
    $keyFile = Join-Path $tmp 'test.key'
    $pw = ConvertTo-SecureString 'throwaway-test-password-1' -AsPlainText -Force
    $wrong = ConvertTo-SecureString 'not-the-password-0000' -AsPlainText -Force

    Write-Host 'Key creation'
    & (Join-Path $PSScriptRoot 'new-release-key.ps1') -KeyFile $keyFile -Header $header -Password $pw | Out-Null
    $key = Get-Content $keyFile -Raw | ConvertFrom-Json
    Check ($key.public -match '^[0-9a-f]{128}$') 'public key written'
    Check ((Get-Content $header -Raw) -match $key.public) 'public key added to the header'
    Check (-not ((Get-Content $keyFile -Raw) -match 'throwaway')) 'password not in the key file'
    Expect-Throw { & (Join-Path $PSScriptRoot 'new-release-key.ps1') -KeyFile $keyFile -Header $header -Password $pw } 'refuses to overwrite a key file'
    Expect-Throw { & (Join-Path $PSScriptRoot 'new-release-key.ps1') -KeyFile (Join-Path $tmp 'short.key') -Header $header -Password (ConvertTo-SecureString 'short' -AsPlainText -Force) } 'refuses a short password'

    Write-Host 'Signing'
    $sums = Join-Path $tmp 'SHA256SUMS.txt'
    $text = "$('ab' * 32)  F1DockTimers-9.0.0-portable.zip`r`n$('cd' * 32)  F1DockTimers-9.0.0-setup.exe`r`n"
    [IO.File]::WriteAllText($sums, $text, [Text.Encoding]::ASCII)
    $sig = Join-Path $tmp 'SHA256SUMS.txt.sig'
    & (Join-Path $PSScriptRoot 'sign-release.ps1') -Version 9.0.0 -KeyFile $keyFile -SumsFile $sums -OutFile $sig -Password $pw -Yes 3>$null | Out-Null
    $bytes = [IO.File]::ReadAllBytes($sig)
    Check ($bytes.Length -eq 64) 'signature is 64 bytes (r||s)'
    Import-Module (Join-Path $PSScriptRoot 'release-key.psm1') -Force
    Check (Test-ReleaseSignature $key.public ([IO.File]::ReadAllBytes($sums)) $bytes) 'signature verifies'
    Check (-not (Test-ReleaseSignature $key.public ([Text.Encoding]::ASCII.GetBytes($text + 'x')) $bytes)) 'a changed list does not verify'
    Expect-Throw { & (Join-Path $PSScriptRoot 'sign-release.ps1') -Version 9.0.0 -KeyFile $keyFile -SumsFile $sums -OutFile (Join-Path $tmp 'w.sig') -Password $wrong -Yes 3>$null } 'wrong password refused'
    Check (-not (Test-Path (Join-Path $tmp 'w.sig'))) 'nothing written with a wrong password'
    Expect-Throw { & (Join-Path $PSScriptRoot 'sign-release.ps1') -Version 9.0.1 -KeyFile $keyFile -SumsFile $sums -OutFile (Join-Path $tmp 'v.sig') -Password $pw -Yes 3>$null } 'refuses a list without that version''s setup'

    if ($WriteFixture) {
        $fx = Join-Path $root 'tests\fixtures\release_sig'
        New-Item -ItemType Directory -Force $fx | Out-Null
        Copy-Item $sums, $sig $fx -Force
        [IO.File]::WriteAllText((Join-Path $fx 'public.hex'), $key.public, [Text.Encoding]::ASCII)
        Write-Host "Fixture written to $fx (a throwaway test key, not the release key)"
    }
} finally {
    Remove-Item $tmp -Recurse -Force
}
if ($failures) { Write-Host "$failures check(s) failed"; exit 1 }
Write-Host 'All release-key checks passed.'
