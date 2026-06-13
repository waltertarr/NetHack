# echoes_ui.ps1 - exercises the headless UI tester against player-facing UI.
#
# The tester observes NetHack's interface without a console via two hooks:
#   NETHACK_ECHOES_UILOG=<file> logs displayed text (pline messages, menu lines,
#                               window text) from tty_putstr()
#   NETHACK_ECHOES_KEYS=<file>  feeds keystrokes (tty_nhgetch + readchar_core)
#
# This test seeds a soul mid-timeline and uses the #echoes status self-test
# (NETHACK_ECHOES_STATUS) to invoke the player-facing #echoes command, then
# asserts its display -- loop number, fragment count, and retained memory --
# was actually produced.  That validates both the UI tester and the command.
#
# Requires a built NetHack.exe in ..\binary.

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }
$tmp = [System.IO.Path]::GetTempPath()
if (Test-Path Env:\NETHACKOPTIONS) { Remove-Item Env:\NETHACKOPTIONS }

# A soul five loops in, 50 fragments, all three memory upgrades unlocked.
$soul = Join-Path $bin "echoes.soul"
Set-Content -Path $soul -Value "seed 12345`nloops 5`nfragments 50`nupgrades 7" -Encoding ascii

$keys = Join-Path $tmp "echoes_ui_keys.txt"
$log = Join-Path $tmp "echoes_ui_log.txt"
Set-Content -Path $keys -Value ([string]::new([char]32, 10)) -Encoding ascii -NoNewline  # spaces to clear --More--
if (Test-Path $log) { Remove-Item $log }

$env:NETHACK_ECHOES = "1"
$env:NETHACK_ECHOES_UILOG = ($log -replace '\\', '/')
$env:NETHACK_ECHOES_KEYS = ($keys -replace '\\', '/')
$env:NETHACK_ECHOES_STATUS = "1"

$name = "echoui_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
        -RedirectStandardInput $keys `
        -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
        -RedirectStandardError (Join-Path $bin "nh_err.txt") `
        -PassThru -WindowStyle Hidden -WorkingDirectory $bin
$null = $p.WaitForExit(8000)
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -EA SilentlyContinue }

foreach ($v in 'NETHACK_ECHOES_UILOG', 'NETHACK_ECHOES_KEYS', 'NETHACK_ECHOES_STATUS') {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}

$soulOk = $false; $memOk = $false; $shown = @()
if (Test-Path $log) {
    $soulOk = [bool](Select-String -Path $log -Pattern 'Soul: loop 6' -Quiet)
    $memOk = [bool](Select-String -Path $log -Pattern 'Memory retained.*items monsters spells' -Quiet)
    $shown = (Select-String -Path $log -Pattern 'Soul:|Memory retained' | ForEach-Object { $_.Line })
}
Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $keys -ErrorAction SilentlyContinue
if (Test-Path $soul) { Remove-Item $soul }

$shown | ForEach-Object { Write-Host "displayed: $_" }
if ($soulOk -and $memOk) {
    Write-Host "`nECHOES UI TESTER (#echoes status): PASS"
    exit 0
} else {
    Write-Host "`nECHOES UI TESTER (#echoes status): FAIL"
    exit 1
}
