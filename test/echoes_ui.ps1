# echoes_ui.ps1 - exercises the headless UI tester.
#
# The tester drives and observes NetHack's interface without a console:
#   NETHACK_ECHOES_KEYS=<file>  feeds keystrokes to tty_nhgetch()
#   NETHACK_ECHOES_UILOG=<file> logs displayed text (pline messages, menu lines,
#                               window text) from tty_putstr()
#
# This test pre-seeds a soul five loops into a timeline, launches a life, and
# asserts the player-facing "your soul remembers (Loop N)" message was actually
# displayed -- proving both the message renders in-game and the tester captures
# real UI output.
#
# Requires a built NetHack.exe in ..\binary.

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }
$tmp = [System.IO.Path]::GetTempPath()
if (Test-Path Env:\NETHACKOPTIONS) { Remove-Item Env:\NETHACKOPTIONS }

# A soul five loops into a timeline, so the next life announces "Loop 6".
$soul = Join-Path $bin "echoes.soul"
Set-Content -Path $soul -Value "seed 12345`nloops 5`nfragments 50`nupgrades 7" -Encoding ascii

$keys = Join-Path $tmp "echoes_ui_keys.txt"
$log = Join-Path $tmp "echoes_ui_log.txt"
Set-Content -Path $keys -Value "      `r`r`r`r" -Encoding ascii -NoNewline  # dismiss --More-- prompts
if (Test-Path $log) { Remove-Item $log }

$env:NETHACK_ECHOES = "1"
$env:NETHACK_ECHOES_UILOG = ($log -replace '\\', '/')
$env:NETHACK_ECHOES_KEYS = ($keys -replace '\\', '/')

$name = "echoui_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
        -RedirectStandardInput $keys `
        -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
        -RedirectStandardError (Join-Path $bin "nh_err.txt") `
        -PassThru -WindowStyle Hidden -WorkingDirectory $bin
Start-Sleep -Seconds 3   # let the scripted life run; then stop it
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -EA SilentlyContinue }

if (Test-Path Env:\NETHACK_ECHOES_UILOG) { Remove-Item Env:\NETHACK_ECHOES_UILOG }
if (Test-Path Env:\NETHACK_ECHOES_KEYS) { Remove-Item Env:\NETHACK_ECHOES_KEYS }

$hit = $false
$shown = "(none)"
if (Test-Path $log) {
    $hit = [bool](Select-String -Path $log -Pattern 'your soul remembers' -Quiet)
    $m = Select-String -Path $log -Pattern 'soul remembers' | Select-Object -First 1
    if ($m) { $shown = $m.Line }
}
Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $keys -ErrorAction SilentlyContinue
if (Test-Path $soul) { Remove-Item $soul }

Write-Host "displayed: $shown"
if ($hit) {
    Write-Host "`nECHOES UI TESTER: PASS"
    exit 0
} else {
    Write-Host "`nECHOES UI TESTER: FAIL"
    exit 1
}
