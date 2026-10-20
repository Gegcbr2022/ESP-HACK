param([string]$Python = "python")
$ErrorActionPreference = "Stop"
$appDirectory = $PSScriptRoot
$tooling = Join-Path $appDirectory ".tooling"
$venvPython = Join-Path $tooling "venv\Scripts\python.exe"
$ufbt = Join-Path $tooling "venv\Scripts\ufbt.exe"
$previousUfbt = $env:UFBT_HOME
try {
    New-Item -ItemType Directory -Force -Path $tooling | Out-Null
    if (!(Test-Path -LiteralPath $venvPython)) {
        & $Python -m venv (Join-Path $tooling "venv")
        if ($LASTEXITCODE -ne 0) { throw "Python venv creation failed" }
    }
    & $venvPython -m pip install "ufbt==0.2.6"
    if ($LASTEXITCODE -ne 0) { throw "uFBT installation failed" }
    $env:UFBT_HOME = Join-Path $tooling "sdk"
    & $ufbt update --branch=1.5.1
    if ($LASTEXITCODE -ne 0) { throw "Official SDK installation failed" }
    Push-Location -LiteralPath $appDirectory
    try {
        & $ufbt
        if ($LASTEXITCODE -ne 0) { throw "FAP build or import validation failed" }
    } finally { Pop-Location }
    Write-Output (Join-Path $appDirectory "dist\esphack_subghz_chat.fap")
} finally { $env:UFBT_HOME = $previousUfbt }
