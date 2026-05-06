param(
  [string]$InstallDir = "$env:ProgramFiles\\Inventory",
  [string]$DataDir = "$env:ProgramData\\Inventory\\data",
  [int]$Port = 8787,
  [string]$BrandName = "Inventory",
  [string]$BrandLogoPath = "",
  [switch]$InstallNow
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Convert-PngToIco {
  param(
    [string]$PngPath,
    [string]$IcoPath
  )

  if (-not (Test-Path $PngPath)) {
    return $false
  }

  Add-Type -AssemblyName System.Drawing
  Add-Type -Namespace Win32 -Name NativeMethods -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Auto)]
public static extern bool DestroyIcon(System.IntPtr handle);
"@

  $bitmap = [System.Drawing.Bitmap]::FromFile($PngPath)
  $iconHandle = $bitmap.GetHicon()

  try {
    $icon = [System.Drawing.Icon]::FromHandle($iconHandle)
    $stream = [System.IO.File]::Open($IcoPath, [System.IO.FileMode]::Create)
    try {
      $icon.Save($stream)
    } finally {
      $stream.Dispose()
      $icon.Dispose()
    }
  } finally {
    $bitmap.Dispose()
    [void][Win32.NativeMethods]::DestroyIcon($iconHandle)
  }

  return (Test-Path $IcoPath)
}

if ($InstallNow -and -not (Test-IsAdministrator)) {
  throw "InstallNow requires Administrator PowerShell to register the SYSTEM startup task."
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$venvDir = Join-Path $scriptDir ".venv"
$pythonExe = Join-Path $venvDir "Scripts\\python.exe"
$distDir = Join-Path $scriptDir "dist"
$buildDir = Join-Path $scriptDir "build"
$specFile = Join-Path $repoRoot "StingrayInventoryDesktop.spec"
$appDistDir = Join-Path $distDir "StingrayInventoryDesktop"
$firmwareIno = Join-Path $repoRoot "firmware\\StingrayInventoryESP32\\StingrayInventoryESP32.ino"
$brandingDir = Join-Path $scriptDir "branding"
$bundledBrandLogo = Join-Path $brandingDir "stingray-logo.png"
$buildIconPath = Join-Path $scriptDir "stingray_desktop.ico"

if (-not (Test-Path $pythonExe)) {
  python -m venv $venvDir
}

& $pythonExe -m pip install --upgrade pip
& $pythonExe -m pip install --upgrade --upgrade-strategy eager -r (Join-Path $scriptDir "requirements.txt")
& $pythonExe -m pip install --upgrade pyinstaller

if ($BrandLogoPath -and (Test-Path $BrandLogoPath)) {
  New-Item -ItemType Directory -Force -Path $brandingDir | Out-Null
  Copy-Item -LiteralPath $BrandLogoPath -Destination $bundledBrandLogo -Force
}

$iconReady = $false
if (Test-Path $bundledBrandLogo) {
  $iconReady = Convert-PngToIco -PngPath $bundledBrandLogo -IcoPath $buildIconPath
}

if (Test-Path $appDistDir) { Remove-Item -Recurse -Force $appDistDir }
if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
if (Test-Path $specFile) { Remove-Item -Force $specFile }

$pyInstallerArgs = @(
  "-m", "PyInstaller",
  "--noconfirm",
  "--clean",
  "--onedir",
  "--name", "StingrayInventoryDesktop",
  "--distpath", $distDir,
  "--workpath", $buildDir,
  "--specpath", $repoRoot,
  "--add-data", "$firmwareIno;firmware\\StingrayInventoryESP32"
)

if ($iconReady) {
  $pyInstallerArgs += @("--icon", $buildIconPath)
}

$pyInstallerArgs += (Join-Path $scriptDir "stingray_desktop_app.py")

& $pythonExe $pyInstallerArgs

if (-not (Test-Path (Join-Path $appDistDir "StingrayInventoryDesktop.exe"))) {
  throw "PyInstaller build failed. EXE not found."
}

$runnerCmdPath = Join-Path $appDistDir "Run-StingrayDesktop.cmd"
$runnerContent = @"
@echo off
setlocal
set APPDIR=%~dp0
set URL=http://127.0.0.1:$Port/
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Invoke-WebRequest -Uri '%URL%api/status' -UseBasicParsing -TimeoutSec 2 | Out-Null; Start-Process '%URL%'; exit 0 } catch { exit 1 }"
if %ERRORLEVEL% EQU 0 goto :done
"%APPDIR%StingrayInventoryDesktop.exe" --host 0.0.0.0 --port $Port --data-dir "$DataDir" --open-browser
:done
endlocal
"@
Set-Content -LiteralPath $runnerCmdPath -Value $runnerContent -Encoding ASCII

Copy-Item -Path (Join-Path $scriptDir "stop_desktop_app.ps1") -Destination (Join-Path $appDistDir "stop_desktop_app.ps1") -Force
Copy-Item -Path (Join-Path $scriptDir "uninstall_desktop_app.ps1") -Destination (Join-Path $appDistDir "uninstall_desktop_app.ps1") -Force
Copy-Item -Path (Join-Path $scriptDir "stop_desktop_app.cmd") -Destination (Join-Path $appDistDir "stop_desktop_app.cmd") -Force
Copy-Item -Path (Join-Path $scriptDir "uninstall_desktop_app.cmd") -Destination (Join-Path $appDistDir "uninstall_desktop_app.cmd") -Force

if (Test-Path $bundledBrandLogo) {
  $appBrandingDir = Join-Path $appDistDir "branding"
  New-Item -ItemType Directory -Force -Path $appBrandingDir | Out-Null
  Copy-Item -LiteralPath $bundledBrandLogo -Destination (Join-Path $appBrandingDir "stingray-logo.png") -Force
}

$bundleDir = Join-Path $distDir "installer_bundle"
if (Test-Path $bundleDir) { Remove-Item -Recurse -Force $bundleDir }
New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null
Copy-Item -Path $appDistDir -Destination (Join-Path $bundleDir "StingrayInventoryDesktop") -Recurse -Force
Copy-Item -Path (Join-Path $scriptDir "install_desktop_app.ps1") -Destination (Join-Path $bundleDir "install_desktop_app.ps1") -Force
Copy-Item -Path (Join-Path $scriptDir "install_desktop_app.cmd") -Destination (Join-Path $bundleDir "Install Inventory.cmd") -Force
Copy-Item -Path (Join-Path $scriptDir "stop_desktop_app.ps1") -Destination (Join-Path $bundleDir "stop_desktop_app.ps1") -Force
Copy-Item -Path (Join-Path $scriptDir "uninstall_desktop_app.ps1") -Destination (Join-Path $bundleDir "uninstall_desktop_app.ps1") -Force
Copy-Item -Path (Join-Path $scriptDir "stop_desktop_app.cmd") -Destination (Join-Path $bundleDir "Stop Inventory.cmd") -Force
Copy-Item -Path (Join-Path $scriptDir "uninstall_desktop_app.cmd") -Destination (Join-Path $bundleDir "Uninstall Inventory.cmd") -Force
Copy-Item -Path (Join-Path $scriptDir "README.md") -Destination (Join-Path $bundleDir "README.md") -Force

$zipPath = Join-Path $distDir "StingrayInventoryDesktop-Installer.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path (Join-Path $bundleDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

if ($InstallNow) {
  & (Join-Path $scriptDir "install_desktop_app.ps1") `
    -SourceDir $appDistDir `
    -InstallDir $InstallDir `
    -DataDir $DataDir `
    -Port $Port `
    -BrandName $BrandName `
    -BrandLogoPath $BrandLogoPath `
    -RunAfterInstall
}

Write-Host "Build complete."
Write-Host "App folder: $appDistDir"
Write-Host "Installer bundle: $bundleDir"
Write-Host "Installer zip: $zipPath"
if (Test-Path $bundledBrandLogo) {
  Write-Host "Bundled brand logo: $bundledBrandLogo"
}
if ($iconReady) {
  Write-Host "EXE icon applied from: $buildIconPath"
}
