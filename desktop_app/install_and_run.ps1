param(
  [string]$HostIp = "0.0.0.0",
  [int]$Port = 8787,
  [string]$DataDir = "$env:ProgramData\\StingrayInventoryDesktop\\data"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$venvDir = Join-Path $scriptDir ".venv"
$pythonExe = Join-Path $venvDir "Scripts\\python.exe"

if (-not (Test-Path $pythonExe)) {
  python -m venv $venvDir
}

try {
  & $pythonExe -m pip install --upgrade pip
  & $pythonExe -m pip install --upgrade --upgrade-strategy eager -r (Join-Path $scriptDir "requirements.txt")
} catch {
  Write-Host "Recreating virtual environment after install failure..."
  if (Test-Path $venvDir) {
    Remove-Item -Recurse -Force $venvDir
  }
  python -m venv $venvDir
  & $pythonExe -m pip install --upgrade pip
  & $pythonExe -m pip install --upgrade --upgrade-strategy eager -r (Join-Path $scriptDir "requirements.txt")
}

& $pythonExe (Join-Path $scriptDir "stingray_desktop_app.py") --host $HostIp --port $Port --data-dir $DataDir --open-browser
