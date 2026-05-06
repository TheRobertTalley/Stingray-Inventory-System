param(
  [string]$InstallDir = "$env:ProgramFiles\\Inventory",
  [string]$DataDir = "$env:ProgramData\\Inventory\\data",
  [string]$SystemTaskName = "Inventory (System Startup)",
  [string]$FirewallRuleName = "Inventory LAN",
  [switch]$RemoveData
)

$ErrorActionPreference = "Stop"
$legacyInstallDirs = @(
  (Join-Path $env:ProgramFiles "StingrayInventoryDesktop"),
  (Join-Path $env:ProgramFiles "Stingray Inventory Desktop")
)

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
    "-SystemTaskName", $SystemTaskName,
    "-FirewallRuleName", $FirewallRuleName
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
  foreach ($taskName in @($SystemTaskName, "Stingray Inventory Desktop (System Startup)")) {
    $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($task) {
      try {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
      } catch {
      }
      Disable-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue | Out-Null
    }
  }
  Get-Process -Name "StingrayInventoryDesktop" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

foreach ($taskName in @($SystemTaskName, "Stingray Inventory Desktop (System Startup)")) {
  $taskToDelete = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
  if ($taskToDelete) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
  }
}

foreach ($ruleName in @($FirewallRuleName, "Stingray Inventory Desktop LAN")) {
  $firewallRule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
  if ($firewallRule) {
    Remove-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
  }
}

$startMenuDir = Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\Inventory"
if (Test-Path $startMenuDir) {
  Remove-Item -LiteralPath $startMenuDir -Recurse -Force -ErrorAction SilentlyContinue
}

foreach ($legacyStartMenuDir in @(
  (Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\StingrayInventoryDesktop"),
  (Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\Stingray Inventory Desktop")
)) {
  if (Test-Path $legacyStartMenuDir) {
    Remove-Item -LiteralPath $legacyStartMenuDir -Recurse -Force -ErrorAction SilentlyContinue
  }
}

$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "Inventory.lnk"
if (Test-Path $desktopShortcutPath) {
  Remove-Item -LiteralPath $desktopShortcutPath -Force -ErrorAction SilentlyContinue
}

foreach ($legacyDesktopShortcutPath in @(
  (Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "Stingray Inventory Desktop.lnk"),
  (Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "StingrayInventoryDesktop.lnk")
)) {
  if (Test-Path $legacyDesktopShortcutPath) {
    Remove-Item -LiteralPath $legacyDesktopShortcutPath -Force -ErrorAction SilentlyContinue
  }
}

$legacyStartupShortcutPaths = @(
  (Join-Path ([Environment]::GetFolderPath("Startup")) "Inventory Background.lnk"),
  (Join-Path ([Environment]::GetFolderPath("CommonStartup")) "Inventory Background.lnk"),
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

if ($RemoveData) {
  $inventoryRoot = Split-Path -Parent $DataDir
  $legacyRoot = Join-Path (Split-Path -Parent $inventoryRoot) "StingrayInventoryDesktop"
  foreach ($root in @($inventoryRoot, $legacyRoot)) {
    if (-not (Test-Path $root)) {
      continue
    }
    foreach ($childName in @("data", "backups", "logs", "config")) {
      $childPath = Join-Path $root $childName
      if (Test-Path $childPath) {
        Remove-Item -LiteralPath $childPath -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
    if (-not (Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue)) {
      Remove-Item -LiteralPath $root -Force -ErrorAction SilentlyContinue
    }
  }
}

foreach ($legacyInstallDir in $legacyInstallDirs) {
  if (Test-Path $legacyInstallDir) {
    Remove-Item -LiteralPath $legacyInstallDir -Recurse -Force -ErrorAction SilentlyContinue
  }
}

if ($RemoveData) {
  Write-Host "Inventory uninstalled. App files and data removed."
} else {
  Write-Host "Inventory uninstalled. Data kept at: $DataDir"
}
