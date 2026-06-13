# echoes_repro.ps1 - headless reproducibility test for "Echoes of the Soul".
#
# Verifies the core time-loop guarantee: a pinned timeline seed regenerates
# the same dungeon.
#   * same seed      -> identical level checksum
#   * different seed -> different checksum (proves the checksum is seed-sensitive)
#
# How it works: with NETHACK_ECHOES_DUMP set, NetHack skips interactive
# character selection (the character is fixed via NETHACKOPTIONS), generates
# level 1 from the pinned seed, writes "seed"/"mapsum" (a checksum of the level
# terrain + floor objects) to the named file, and exits before the game loop.
# A fresh GUID player name per run avoids save/lock-file collisions.
#
# Requires a built NetHack.exe in ..\binary (run the nmake build first).
# Usage:  powershell -ExecutionPolicy Bypass -File test\echoes_repro.ps1

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }

$tmp = [System.IO.Path]::GetTempPath()
$inp = Join-Path $bin "nh_in.txt"
Set-Content -Path $inp -Value (([char]10).ToString() * 20) -Encoding ascii -NoNewline

$env:NETHACK_ECHOES = "1"
$env:NETHACKOPTIONS = "role:Valkyrie,race:human,gender:female,align:lawful"

function Get-MapSum {
    $name = "echotest_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
    $dump = Join-Path $tmp ($name + ".txt")
    $env:NETHACK_ECHOES_DUMP = ($dump -replace '\\', '/')
    $p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
            -RedirectStandardInput $inp `
            -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
            -RedirectStandardError (Join-Path $bin "nh_err.txt") `
            -PassThru -WindowStyle Hidden -WorkingDirectory $bin
    $null = $p.WaitForExit(10000)
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -EA SilentlyContinue }
    $line = (Get-Content $dump -EA SilentlyContinue) | Where-Object { $_ -match '^mapsum' }
    Remove-Item $dump -EA SilentlyContinue
    return $line
}

$soul = Join-Path $bin "echoes.soul"

# Test 1: same timeline seed must reproduce the dungeon.
if (Test-Path $soul) { Remove-Item $soul }
$a = Get-MapSum   # mints a new timeline seed and saves it
$b = Get-MapSum   # reuses the saved seed
$sameSeedOk = [bool]($a -and ($a -eq $b))

# Test 2: a different timeline seed must give a different dungeon.
if (Test-Path $soul) { Remove-Item $soul }
$c = Get-MapSum   # mints a different seed
$diffSeedOk = [bool]($a -and ($a -ne $c))

Write-Host "same-seed run 1 : $a"
Write-Host "same-seed run 2 : $b"
Write-Host "diff-seed run   : $c"
Write-Host ""
Write-Host ("[{0}] same seed -> identical dungeon" -f $(if ($sameSeedOk) { "PASS" } else { "FAIL" }))
Write-Host ("[{0}] different seed -> different dungeon" -f $(if ($diffSeedOk) { "PASS" } else { "FAIL" }))

if ($sameSeedOk -and $diffSeedOk) {
    Write-Host "`nECHOES REPRODUCIBILITY: PASS"
    exit 0
} else {
    Write-Host "`nECHOES REPRODUCIBILITY: FAIL"
    exit 1
}
