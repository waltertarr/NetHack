# echoes_knowledge.ps1 - verifies the memory tree GATES which knowledge the
# soul retains across a loop.
#
#   Scenario A: with the memory upgrades unlocked, an injected monster + spell
#               survive a death/loop.
#   Scenario B: without the upgrades, that same knowledge is NOT retained.
#
# Each scenario uses the two-phase headless self-test: phase 1 (KINJECT) learns
# a fixed test monster + spell and "dies" (persisting the soul); phase 2
# (KVERIFY) restores the soul in a fresh life and reports what survived.
# NETHACK_ECHOES_BUY=<bitmask> unlocks upgrades for the test (1=items 2=monsters
# 4=spells).
#
# Requires a built NetHack.exe in ..\binary.

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

function Invoke-NH($envName, $envVal) {
    $name = "echoknow_" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
    Set-Item -Path "Env:$envName" -Value $envVal
    $p = Start-Process -FilePath $exe -ArgumentList "-u", $name `
            -RedirectStandardInput $inp `
            -RedirectStandardOutput (Join-Path $bin "nh_out.txt") `
            -RedirectStandardError (Join-Path $bin "nh_err.txt") `
            -PassThru -WindowStyle Hidden -WorkingDirectory $bin
    $null = $p.WaitForExit(10000)
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -EA SilentlyContinue }
    Remove-Item -Path "Env:$envName" -EA SilentlyContinue
}

function Test-Knowledge {
    # inject + verify against a fresh timeline; return "<mon> <spell>" as True/False
    if (Test-Path $soul) { Remove-Item $soul }
    Invoke-NH "NETHACK_ECHOES_KINJECT" "1"
    $result = Join-Path $tmp ("echoknow_" + ([guid]::NewGuid().ToString("N").Substring(0, 8)) + ".txt")
    Invoke-NH "NETHACK_ECHOES_KVERIFY" ($result -replace '\\', '/')
    $lines = Get-Content $result -ErrorAction SilentlyContinue
    Remove-Item $result -ErrorAction SilentlyContinue
    $mon = [bool]($lines | Where-Object { $_ -eq 'mon 1' })
    $spell = [bool]($lines | Where-Object { $_ -eq 'spell 1' })
    return "$mon $spell"
}

# Scenario A: upgrades unlocked (items|monsters|spells = 7).
$env:NETHACK_ECHOES_BUY = "7"
$on = (Test-Knowledge) -split ' '

# Scenario B: no upgrades unlocked.
Remove-Item Env:\NETHACK_ECHOES_BUY -ErrorAction SilentlyContinue
$off = (Test-Knowledge) -split ' '

Write-Host ("with upgrades   : mon={0,-5} spell={1,-5} (expect True  True)" -f $on[0], $on[1])
Write-Host ("without upgrades: mon={0,-5} spell={1,-5} (expect False False)" -f $off[0], $off[1])

$ok = ($on[0] -eq 'True') -and ($on[1] -eq 'True') -and ($off[0] -eq 'False') -and ($off[1] -eq 'False')
if ($ok) {
    Write-Host "`nECHOES MEMORY TREE GATING: PASS"
    exit 0
} else {
    Write-Host "`nECHOES MEMORY TREE GATING: FAIL"
    exit 1
}
