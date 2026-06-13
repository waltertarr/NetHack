# echoes_knowledge.ps1 - verifies the soul's KNOWLEDGE persists across a loop.
#
# Two headless phases against a fresh timeline:
#   phase 1 (KINJECT) - a new life learns a fixed test monster + spell, then
#                       "dies" (which persists the soul) and exits.
#   phase 2 (KVERIFY) - the next life restores the soul; the test reports whether
#                       that monster knowledge and spell survived the loop.
#
# Requires a built NetHack.exe in ..\binary.
# Usage:  powershell -ExecutionPolicy Bypass -File test\echoes_knowledge.ps1

$ErrorActionPreference = "Stop"
$bin = Resolve-Path (Join-Path $PSScriptRoot "..\binary")
$exe = Join-Path $bin "NetHack.exe"
if (-not (Test-Path $exe)) { Write-Error "NetHack.exe not found at $exe - build it first."; exit 2 }

$tmp = [System.IO.Path]::GetTempPath()
$inp = Join-Path $bin "nh_in.txt"
Set-Content -Path $inp -Value (([char]10).ToString() * 20) -Encoding ascii -NoNewline

$env:NETHACK_ECHOES = "1"
# The character is forced by Echoes mode (the Soulbound Wanderer); clear any
# stale NETHACKOPTIONS so it can't influence the run.
Remove-Item Env:\NETHACKOPTIONS -ErrorAction SilentlyContinue
$soul = Join-Path $bin "echoes.soul"

function Run-NH($envName, $envVal) {
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

# Fresh timeline, then inject knowledge and die.
if (Test-Path $soul) { Remove-Item $soul }
Run-NH "NETHACK_ECHOES_KINJECT" "1"

# Next life: verify the knowledge was restored.
$result = Join-Path $tmp ("echoknow_" + ([guid]::NewGuid().ToString("N").Substring(0, 8)) + ".txt")
Run-NH "NETHACK_ECHOES_KVERIFY" ($result -replace '\\', '/')

$lines = Get-Content $result -EA SilentlyContinue
Remove-Item $result -EA SilentlyContinue
$monOk = [bool]($lines | Where-Object { $_ -eq 'mon 1' })
$spellOk = [bool]($lines | Where-Object { $_ -eq 'spell 1' })

Write-Host "verify output: $($lines -join '; ')"
Write-Host ("[{0}] monster knowledge persisted across the loop" -f $(if ($monOk) { "PASS" } else { "FAIL" }))
Write-Host ("[{0}] spell knowledge persisted across the loop" -f $(if ($spellOk) { "PASS" } else { "FAIL" }))

if ($monOk -and $spellOk) {
    Write-Host "`nECHOES KNOWLEDGE PERSISTENCE: PASS"
    exit 0
} else {
    Write-Host "`nECHOES KNOWLEDGE PERSISTENCE: FAIL"
    exit 1
}
