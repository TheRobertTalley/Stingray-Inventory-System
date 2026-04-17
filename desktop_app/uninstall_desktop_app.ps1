param(
  [string]$InstallDir = "$env:ProgramFiles\\StingrayInventoryDesktop",
  [string]$DataDir = "$env:ProgramData\\StingrayInventoryDesktop\\data",
  [string]$SystemTaskName = "Stingray Inventory Desktop (System Startup)",
  [switch]$RemoveData
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Resolve-PowerShellExe {
  $powershellCommand = Get-Command powershell.exe -ErrorAction SilentlyContinue
  if ($powershellCommand -and (Test-Path $powershellCommand.Source)) {
    return $powershellCommand.Source
  }
  return (Join-Path $env:WINDIR "System32\\WindowsPowerShell\\v1.0\\powershell.exe")
}

if (-not (Test-IsAdministrator)) {
  $powershellExe = Resolve-PowerShellExe
  $elevatedArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $PSCommandPath,
    "-InstallDir", $InstallDir,
    "-DataDir", $DataDir,
    "-SystemTaskName", $SystemTaskName
  )
  if ($RemoveData) { $elevatedArgs += "-RemoveData" }

  try {
    $proc = Start-Process -FilePath $powershellExe -Verb RunAs -ArgumentList $elevatedArgs -PassThru -Wait
    exit $proc.ExitCode
  } catch {
    throw "Administrator approval is required to uninstall."
  }
}

$stopScript = Join-Path $InstallDir "stop_desktop_app.ps1"
if (Test-Path $stopScript) {
  & $stopScript -InstallDir $InstallDir -SystemTaskName $SystemTaskName | Out-Null
} else {
  $task = Get-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue
  if ($task) {
    try {
      Stop-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue
    } catch {
    }
    Disable-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue | Out-Null
  }
  Get-Process -Name "StingrayInventoryDesktop" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

$taskToDelete = Get-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue
if ($taskToDelete) {
  Unregister-ScheduledTask -TaskName $SystemTaskName -Confirm:$false -ErrorAction SilentlyContinue
}

$startMenuDir = Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\Stingray Inventory Desktop"
if (Test-Path $startMenuDir) {
  Remove-Item -LiteralPath $startMenuDir -Recurse -Force -ErrorAction SilentlyContinue
}

$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "Stingray Inventory Desktop.lnk"
if (Test-Path $desktopShortcutPath) {
  Remove-Item -LiteralPath $desktopShortcutPath -Force -ErrorAction SilentlyContinue
}

$legacyStartupShortcutPaths = @(
  (Join-Path ([Environment]::GetFolderPath("Startup")) "Stingray Inventory Desktop Background.lnk"),
  (Join-Path ([Environment]::GetFolderPath("CommonStartup")) "Stingray Inventory Desktop Background.lnk")
)
foreach ($legacyPath in $legacyStartupShortcutPaths) {
  if (Test-Path $legacyPath) {
    Remove-Item -LiteralPath $legacyPath -Force -ErrorAction SilentlyContinue
  }
}

# Delete install directory after this script exits.
if (Test-Path $InstallDir) {
  $removeScriptPath = Join-Path $env:TEMP ("stingray-uninstall-" + [Guid]::NewGuid().ToString("N") + ".cmd")
  $removeCmd = @"
@echo off
timeout /t 2 /nobreak >nul
rmdir /s /q "$InstallDir"
del /f /q "%~f0"
"@
  Set-Content -LiteralPath $removeScriptPath -Value $removeCmd -Encoding ASCII
  Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$removeScriptPath`"" -WindowStyle Hidden
}

if ($RemoveData -and (Test-Path $DataDir)) {
  Remove-Item -LiteralPath $DataDir -Recurse -Force -ErrorAction SilentlyContinue
  $parentData = Split-Path -Parent $DataDir
  if (Test-Path $parentData -and -not (Get-ChildItem -LiteralPath $parentData -Force -ErrorAction SilentlyContinue)) {
    Remove-Item -LiteralPath $parentData -Force -ErrorAction SilentlyContinue
  }
}

if ($RemoveData) {
  Write-Host "Stingray Inventory Desktop uninstalled. App files and data removed."
} else {
  Write-Host "Stingray Inventory Desktop uninstalled. Data kept at: $DataDir"
}
