# echoes_loop.ps1 - verifies auto-loop on death (re-exec) works.
#
# Sets a target N.  Each life advances the loop counter and relaunches the
# executable (the real re-exec path) until N loops have run, then writes the
# final loop count.  This exercises the re-exec + lock + soul-carry chain
# across N separate processes without needing interactive gameplay.
#
# Requires a built NetHack.exe in ..\binary.
# Usage:  powershell -ExecutionPolicy Bypass -File test\echoes_loop.ps1

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }

$tmp = [System.IO.Path]::GetTempPath()
$inp = Join-Path $bin "nh_in.txt"
Set-Content -Path $inp -Value (([char]10).ToString() * 20) -Encoding ascii -NoNewline
Remove-Item Env:\NETHACKOPTIONS -ErrorAction SilentlyContinue

$env:NETHACK_ECHOES = "1"
$soul = Join-Path $bin "echoes.soul"
if (Test-Path $soul) { Remove-Item $soul }

$target = 4
$result = Join-Path $tmp ("echoloop_" + ([guid]::NewGuid().ToString("N").Substring(0, 8)) + ".txt")
$env:NETHACK_ECHOES_REEXEC_TARGET = "$target"
$env:NETHACK_ECHOES_REEXEC_RESULT = ($result -replace '\\', '/')

$name = "echoloop_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
        -RedirectStandardInput $inp `
        -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
        -RedirectStandardError (Join-Path $bin "nh_err.txt") `
        -PassThru -WindowStyle Hidden -WorkingDirectory $bin
$null = $p.WaitForExit(10000)

# The loop runs across N separate (re-exec'd) processes, so wait for the
# final process to write the result rather than just the first to exit.
$deadline = (Get-Date).AddSeconds(20)
while (-not (Test-Path $result) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }

Remove-Item Env:\NETHACK_ECHOES_REEXEC_TARGET -ErrorAction SilentlyContinue
Remove-Item Env:\NETHACK_ECHOES_REEXEC_RESULT -ErrorAction SilentlyContinue

$content = Get-Content $result -ErrorAction SilentlyContinue
Remove-Item $result -ErrorAction SilentlyContinue
$loopLine = $content | Where-Object { $_ -match '^loops' }
$fragLine = $content | Where-Object { $_ -match '^fragments' }
$got = if ($loopLine -match 'loops (\d+)') { [int]$Matches[1] } else { -1 }
$frags = if ($fragLine -match 'fragments (\d+)') { [int]$Matches[1] } else { -1 }

Write-Host "target loops: $target ; reached: $got ; memory fragments: $frags"
if ($got -eq $target -and $frags -gt 0) {
    Write-Host "`nECHOES AUTO-LOOP (+ fragment accrual): PASS"
    exit 0
} else {
    Write-Host "`nECHOES AUTO-LOOP: FAIL"
    exit 1
}
