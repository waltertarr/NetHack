# echoes_summary.ps1 - verifies the death summary is shown when a life ends.
#
# Uses the death-summary self-test (NETHACK_ECHOES_SUMMARY) to render the
# summary headlessly and the UI transcript (NETHACK_ECHOES_UILOG) to capture
# it, asserting both summary lines are produced.
#
# Requires a built NetHack.exe in ..\binary.

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }
$tmp = [System.IO.Path]::GetTempPath()
if (Test-Path Env:\NETHACKOPTIONS) { Remove-Item Env:\NETHACKOPTIONS }

$soul = Join-Path $bin "echoes.soul"
Set-Content -Path $soul -Value "seed 777`nloops 3`nfragments 10`nupgrades 7" -Encoding ascii

$keys = Join-Path $tmp "echoes_sum_keys.txt"
$log = Join-Path $tmp "echoes_sum_log.txt"
Set-Content -Path $keys -Value ([string]::new([char]32, 10)) -Encoding ascii -NoNewline  # clear --More--
if (Test-Path $log) { Remove-Item $log }

$env:NETHACK_ECHOES = "1"
$env:NETHACK_ECHOES_UILOG = ($log -replace '\\', '/')
$env:NETHACK_ECHOES_KEYS = ($keys -replace '\\', '/')
$env:NETHACK_ECHOES_SUMMARY = "1"

$name = "echosum_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
        -RedirectStandardInput $keys `
        -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
        -RedirectStandardError (Join-Path $bin "nh_err.txt") `
        -PassThru -WindowStyle Hidden -WorkingDirectory $bin
$null = $p.WaitForExit(8000)
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -EA SilentlyContinue }

foreach ($v in 'NETHACK_ECHOES_UILOG', 'NETHACK_ECHOES_KEYS', 'NETHACK_ECHOES_SUMMARY') {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}

$fades = $false; $ends = $false; $shown = @()
if (Test-Path $log) {
    $fades = [bool](Select-String -Path $log -Pattern 'Your soul fades' -Quiet)
    $ends = [bool](Select-String -Path $log -Pattern 'Loop 4 ends' -Quiet)
    $shown = (Select-String -Path $log -Pattern 'soul fades|Loop 4 ends' | ForEach-Object { $_.Line })
}
Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $keys -ErrorAction SilentlyContinue
if (Test-Path $soul) { Remove-Item $soul }

$shown | ForEach-Object { Write-Host "displayed: $_" }
if ($fades -and $ends) {
    Write-Host "`nECHOES DEATH SUMMARY: PASS"
    exit 0
} else {
    Write-Host "`nECHOES DEATH SUMMARY: FAIL"
    exit 1
}
