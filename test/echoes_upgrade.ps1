# echoes_upgrade.ps1 - player-driven memory tree.
#
# Verifies the soul is OFFERED an affordable memory upgrade at the start of a
# life and that the player's choice is honoured:
#   accept (y) -> the upgrade is purchased ("learns to remember items")
#   decline (n) -> the upgrade is NOT purchased
#
# Driven via the headless UI tester (scripted keys + transcript capture).
# Requires a built NetHack.exe in ..\binary.

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }
$tmp = [System.IO.Path]::GetTempPath()
if (Test-Path Env:\NETHACKOPTIONS) { Remove-Item Env:\NETHACKOPTIONS }

$soul = Join-Path $bin "echoes.soul"
$keys = Join-Path $tmp "echoes_up_keys.txt"
$log = Join-Path $tmp "echoes_up_log.txt"
$env:NETHACK_ECHOES = "1"
$env:NETHACK_ECHOES_UILOG = ($log -replace '\\', '/')
$env:NETHACK_ECHOES_KEYS = ($keys -replace '\\', '/')

function Invoke-Offer($answerChar) {
    # soul with exactly enough fragments (4) for the cheapest upgrade (items, 3)
    Set-Content -Path $soul -Value "seed 999`nloops 2`nfragments 4`nupgrades 0" -Encoding ascii
    Set-Content -Path $keys -Value ([string]::new([char]$answerChar, 6)) -Encoding ascii -NoNewline
    if (Test-Path $log) { Remove-Item $log }
    $name = "echoup_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
    $p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
            -RedirectStandardInput $keys `
            -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
            -RedirectStandardError (Join-Path $bin "nh_err.txt") `
            -PassThru -WindowStyle Hidden -WorkingDirectory $bin
    Start-Sleep -Seconds 3
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -EA SilentlyContinue }
    $offered = [bool](Select-String -Path $log -Pattern 'Spend 3 memory fragments to remember items' -Quiet)
    $bought = [bool](Select-String -Path $log -Pattern 'learns to remember items' -Quiet)
    return "$offered $bought"
}

$accept = (Invoke-Offer 121) -split ' '   # 'y'
$decline = (Invoke-Offer 110) -split ' '   # 'n'

foreach ($f in $soul, $keys, $log) { if (Test-Path $f) { Remove-Item $f } }
foreach ($v in 'NETHACK_ECHOES_UILOG', 'NETHACK_ECHOES_KEYS') { if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" } }

Write-Host ("accept (y) : offered={0} bought={1}  (expect True True)" -f $accept[0], $accept[1])
Write-Host ("decline (n): offered={0} bought={1}  (expect True False)" -f $decline[0], $decline[1])

$ok = ($accept[0] -eq 'True') -and ($accept[1] -eq 'True') `
      -and ($decline[0] -eq 'True') -and ($decline[1] -eq 'False')
if ($ok) {
    Write-Host "`nECHOES MEMORY-TREE OFFER: PASS"
    exit 0
} else {
    Write-Host "`nECHOES MEMORY-TREE OFFER: FAIL"
    exit 1
}
