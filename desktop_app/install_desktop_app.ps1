param(
  [string]$SourceDir = "",
  [string]$InstallDir = "$env:ProgramFiles\\Inventory",
  [string]$DataDir = "$env:ProgramData\\Inventory\\data",
  [int]$Port = 8787,
  [string]$BrandName = "Inventory",
  [string]$BrandLogoPath = "",
  [switch]$NoDesktopShortcut,
  [switch]$NoAutoStart,
  [string]$SystemTaskName = "Inventory (System Startup)",
  [string]$FirewallRuleName = "Inventory LAN",
  [switch]$RunAfterInstall
)

$ErrorActionPreference = "Stop"

$cloudHeader = "provider|login_email|folder_name|folder_hint|mode|backup_mode|asset_mode|brand_name|brand_logo_ref|client_id|client_secret|updated_at"
$legacySystemTaskName = "Stingray Inventory Desktop (System Startup)"
$legacyFirewallRuleName = "Stingray Inventory Desktop LAN"
$legacyInstallDirs = @(
  (Join-Path $env:ProgramFiles "StingrayInventoryDesktop"),
  (Join-Path $env:ProgramFiles "Stingray Inventory Desktop")
)
$legacyProgramDataRoots = @(
  (Join-Path $env:ProgramData "StingrayInventoryDesktop"),
  (Join-Path $env:ProgramData "Stingray Inventory Desktop")
)
$legacyStartMenuDirs = @(
  (Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\StingrayInventoryDesktop"),
  (Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\Stingray Inventory Desktop")
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

function Resolve-BrandLogoPath {
  param(
    [string]$ExplicitLogoPath,
    [string]$InstallSourceDir
  )

  if ($ExplicitLogoPath -and (Test-Path $ExplicitLogoPath)) {
    return (Resolve-Path $ExplicitLogoPath).Path
  }

  $bundledLogoPath = Join-Path $InstallSourceDir "branding\\stingray-logo.png"
  if (Test-Path $bundledLogoPath) {
    return (Resolve-Path $bundledLogoPath).Path
  }

  return ""
}

function Copy-MissingTree {
  param(
    [string]$SourcePath,
    [string]$DestinationPath
  )

  if (-not (Test-Path $SourcePath)) {
    return $false
  }

  New-Item -ItemType Directory -Force -Path $DestinationPath | Out-Null
  foreach ($entry in Get-ChildItem -LiteralPath $SourcePath -Force -ErrorAction SilentlyContinue) {
    $target = Join-Path $DestinationPath $entry.Name
    if ($entry.PSIsContainer) {
      [void](Copy-MissingTree -SourcePath $entry.FullName -DestinationPath $target)
    } elseif (-not (Test-Path $target)) {
      Copy-Item -LiteralPath $entry.FullName -Destination $target -Force
    }
  }

  return $true
}

function Import-LegacyProgramData {
  param(
    [string[]]$LegacyRoots,
    [string]$InventoryRoot
  )

  $imported = $false
  foreach ($legacyRoot in $LegacyRoots) {
    if (-not (Test-Path $legacyRoot)) {
      continue
    }

    $imported = $true
    foreach ($relativeChild in @("data", "backups", "logs", "config")) {
      $legacyChild = Join-Path $legacyRoot $relativeChild
      $newChild = Join-Path $InventoryRoot $relativeChild
      [void](Copy-MissingTree -SourcePath $legacyChild -DestinationPath $newChild)
    }
  }

  return $imported
}

function Remove-DirectoryTree {
  param(
    [string]$Path
  )

  if (-not (Test-Path $Path)) {
    return
  }

  try {
    Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
  } catch {
  }

  try {
    Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  } catch {
  }
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

function Update-CloudBranding {
  param(
    [string]$CloudConfigPath,
    [string]$NewBrandName,
    [string]$NewBrandLogoRef
  )

  $fields = @("google_drive", "", "", "", "select_or_create", "sd_only", "sd_only", "Stingray Inventory", "", "", "", "")

  if (Test-Path $CloudConfigPath) {
    $lines = Get-Content -LiteralPath $CloudConfigPath -Encoding UTF8
    $payloadLine = $lines | Where-Object { $_ -and -not $_.StartsWith("provider|") } | Select-Object -First 1
    if ($payloadLine) {
      $parts = $payloadLine.Split("|")
      $limit = [Math]::Min($parts.Count, $fields.Count)
      for ($i = 0; $i -lt $limit; $i++) {
        $fields[$i] = $parts[$i]
      }
    }
  }

  $fields[7] = $NewBrandName
  if ($NewBrandLogoRef) {
    $fields[8] = $NewBrandLogoRef
  }
  $fields[11] = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

  $content = $cloudHeader + "`n" + ($fields -join "|") + "`n"
  Set-Content -LiteralPath $CloudConfigPath -Value $content -Encoding UTF8
}

if (-not $SourceDir) {
  $bundleCandidate = Join-Path $PSScriptRoot "StingrayInventoryDesktop"
  $repoCandidate = Join-Path $PSScriptRoot "dist\\StingrayInventoryDesktop"
  if (Test-Path (Join-Path $bundleCandidate "StingrayInventoryDesktop.exe")) {
    $SourceDir = $bundleCandidate
  } elseif (Test-Path (Join-Path $repoCandidate "StingrayInventoryDesktop.exe")) {
    $SourceDir = $repoCandidate
  } else {
    $SourceDir = $bundleCandidate
  }
}

if (Test-Path $SourceDir) {
  $SourceDir = (Resolve-Path $SourceDir).Path
}

if (-not (Test-IsAdministrator)) {
  $powershellExe = Resolve-PowerShellExe
  $elevatedArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $PSCommandPath,
    "-SourceDir", $SourceDir,
    "-InstallDir", $InstallDir,
    "-DataDir", $DataDir,
    "-Port", $Port.ToString(),
    "-BrandName", $BrandName,
    "-BrandLogoPath", $BrandLogoPath,
    "-SystemTaskName", $SystemTaskName,
    "-FirewallRuleName", $FirewallRuleName
  )
  if ($NoDesktopShortcut) { $elevatedArgs += "-NoDesktopShortcut" }
  if ($NoAutoStart) { $elevatedArgs += "-NoAutoStart" }
  if ($RunAfterInstall) { $elevatedArgs += "-RunAfterInstall" }

  try {
    $proc = Start-Process -FilePath $powershellExe -Verb RunAs -ArgumentList $elevatedArgs -PassThru -Wait
    exit $proc.ExitCode
  } catch {
    throw "Administrator approval is required for installation. Run was canceled or failed to elevate."
  }
}

if (-not (Test-Path $SourceDir)) {
  throw "Source directory not found: $SourceDir"
}

if (-not (Test-Path (Join-Path $SourceDir "StingrayInventoryDesktop.exe"))) {
  throw "StingrayInventoryDesktop.exe not found in: $SourceDir"
}

# Stop running app so in-use DLLs do not block installation updates.
$runningApp = Get-Process -Name "StingrayInventoryDesktop" -ErrorAction SilentlyContinue
if ($runningApp) {
  $runningApp | Stop-Process -Force
  Start-Sleep -Milliseconds 300
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $env:ProgramData "Inventory\\logs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $env:ProgramData "Inventory\\config") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DataDir "images") | Out-Null

$inventoryRoot = Split-Path -Parent $DataDir
[void](Import-LegacyProgramData -LegacyRoots $legacyProgramDataRoots -InventoryRoot $inventoryRoot)

foreach ($legacyInstallDir in $legacyInstallDirs) {
  if ($legacyInstallDir -and (Test-Path $legacyInstallDir)) {
    Remove-DirectoryTree -Path $legacyInstallDir
  }
}

foreach ($legacyStartMenuDir in $legacyStartMenuDirs) {
  if ($legacyStartMenuDir -and (Test-Path $legacyStartMenuDir)) {
    Remove-DirectoryTree -Path $legacyStartMenuDir
  }
}

Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $InstallDir -Recurse -Force

$existingFirewallRule = Get-NetFirewallRule -DisplayName $FirewallRuleName -ErrorAction SilentlyContinue
if ($existingFirewallRule) {
  Remove-NetFirewallRule -DisplayName $FirewallRuleName -ErrorAction SilentlyContinue
}
$legacyFirewallRule = Get-NetFirewallRule -DisplayName $legacyFirewallRuleName -ErrorAction SilentlyContinue
if ($legacyFirewallRule) {
  Remove-NetFirewallRule -DisplayName $legacyFirewallRuleName -ErrorAction SilentlyContinue
}
New-NetFirewallRule `
  -DisplayName $FirewallRuleName `
  -Direction Inbound `
  -Action Allow `
  -Protocol TCP `
  -LocalPort $Port `
  -Profile Private,Public `
  -Description "Allow Inventory from devices on the local LAN, including Windows networks classified as Public." | Out-Null

$mainPageUrl = "http://127.0.0.1:$Port/"
$launcherPath = Join-Path $InstallDir "Run-StingrayDesktop.cmd"
$launcherContent = @"
@echo off
setlocal
set APPDIR=%~dp0
set URL=$mainPageUrl
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Invoke-WebRequest -Uri '%URL%api/status' -UseBasicParsing -TimeoutSec 2 | Out-Null; Start-Process '%URL%'; exit 0 } catch { exit 1 }"
if %ERRORLEVEL% EQU 0 goto :done
"%APPDIR%StingrayInventoryDesktop.exe" --host 0.0.0.0 --port $Port --data-dir "$DataDir" --open-browser
:done
endlocal
"@
Set-Content -LiteralPath $launcherPath -Value $launcherContent -Encoding ASCII

$supervisorPath = Join-Path $InstallDir "StingrayDesktopSupervisor.ps1"
$supervisorTemplate = @'
$ErrorActionPreference = "SilentlyContinue"
$mutexName = "Local\StingrayInventoryDesktopSupervisor"
$createdNew = $false
$mutex = New-Object System.Threading.Mutex($true, $mutexName, [ref]$createdNew)
if (-not $createdNew) {
  exit 0
}

$appDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $appDir "StingrayInventoryDesktop.exe"
$args = @("--host", "0.0.0.0", "--port", "__PORT__", "--data-dir", "__DATADIR__")

while ($true) {
  $running = Get-Process -Name "StingrayInventoryDesktop" -ErrorAction SilentlyContinue
  if ($running) {
    Start-Sleep -Seconds 3
    continue
  }

  try {
    $process = Start-Process -FilePath $exePath -ArgumentList $args -PassThru
    Wait-Process -Id $process.Id
  } catch {
  }

  Start-Sleep -Seconds 2
}
'@
$supervisorContent = $supervisorTemplate.Replace("__PORT__", [string]$Port).Replace("__DATADIR__", $DataDir)
Set-Content -LiteralPath $supervisorPath -Value $supervisorContent -Encoding UTF8

$resolvedLogoPath = Resolve-BrandLogoPath -ExplicitLogoPath $BrandLogoPath -InstallSourceDir $SourceDir
$brandLogoRef = ""
$iconPath = Join-Path $InstallDir "StingrayInventoryDesktop.ico"

if ($resolvedLogoPath) {
  $extension = [System.IO.Path]::GetExtension($resolvedLogoPath).ToLowerInvariant()
  if (-not $extension) {
    $extension = ".png"
  }
  $savedLogoName = "stingray-logo$extension"
  $savedLogoPath = Join-Path (Join-Path $DataDir "images") $savedLogoName
  Copy-Item -LiteralPath $resolvedLogoPath -Destination $savedLogoPath -Force
  $encodedStoragePath = [System.Uri]::EscapeDataString("/images/$savedLogoName")
  $brandLogoRef = "/api/files?path=$encodedStoragePath"
  [void](Convert-PngToIco -PngPath $savedLogoPath -IcoPath $iconPath)
}

$cloudConfigPath = Join-Path $env:ProgramData "Inventory\\config\\cloud_backup.cfg"
Update-CloudBranding -CloudConfigPath $cloudConfigPath -NewBrandName $BrandName -NewBrandLogoRef $brandLogoRef

$shortcutIcon = if (Test-Path $iconPath) { $iconPath } else { Join-Path $InstallDir "StingrayInventoryDesktop.exe" }

$wsh = New-Object -ComObject WScript.Shell
$startMenuDir = Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\Inventory"
New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null

$startShortcutPath = Join-Path $startMenuDir "Inventory.lnk"
$startShortcut = $wsh.CreateShortcut($startShortcutPath)
$startShortcut.TargetPath = $launcherPath
$startShortcut.WorkingDirectory = $InstallDir
$startShortcut.IconLocation = $shortcutIcon
$startShortcut.Description = "Open Inventory main page"
$startShortcut.Save()

$manualShortcutPath = Join-Path $startMenuDir "Inventory (Manual Start).lnk"
$manualShortcut = $wsh.CreateShortcut($manualShortcutPath)
$manualShortcut.TargetPath = $launcherPath
$manualShortcut.WorkingDirectory = $InstallDir
$manualShortcut.IconLocation = $shortcutIcon
$manualShortcut.Description = "Start Inventory desktop app and open browser"
$manualShortcut.Save()

$powershellExe = Resolve-PowerShellExe

$stopScriptPath = Join-Path $InstallDir "stop_desktop_app.ps1"
$stopShortcutPath = Join-Path $startMenuDir "Stop Inventory.lnk"
$stopShortcut = $wsh.CreateShortcut($stopShortcutPath)
$stopShortcut.TargetPath = $powershellExe
$stopShortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$stopScriptPath`" -SystemTaskName `"$SystemTaskName`""
$stopShortcut.WorkingDirectory = $InstallDir
$stopShortcut.IconLocation = $shortcutIcon
$stopShortcut.Description = "Stop Inventory and disable startup task"
$stopShortcut.Save()

$uninstallScriptPath = Join-Path $InstallDir "uninstall_desktop_app.ps1"
$uninstallShortcutPath = Join-Path $startMenuDir "Uninstall Inventory.lnk"
$uninstallShortcut = $wsh.CreateShortcut($uninstallShortcutPath)
$uninstallShortcut.TargetPath = $powershellExe
$uninstallShortcut.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$uninstallScriptPath`" -InstallDir `"$InstallDir`" -DataDir `"$DataDir`" -SystemTaskName `"$SystemTaskName`" -FirewallRuleName `"$FirewallRuleName`""
$uninstallShortcut.WorkingDirectory = $InstallDir
$uninstallShortcut.IconLocation = $shortcutIcon
$uninstallShortcut.Description = "Uninstall Inventory (keeps data by default)"
$uninstallShortcut.Save()

$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "Inventory.lnk"
if (-not $NoDesktopShortcut) {
  $desktopShortcut = $wsh.CreateShortcut($desktopShortcutPath)
  $desktopShortcut.TargetPath = $launcherPath
  $desktopShortcut.WorkingDirectory = $InstallDir
  $desktopShortcut.IconLocation = $shortcutIcon
  $desktopShortcut.Description = "Open Inventory main page"
  $desktopShortcut.Save()
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

$legacyTask = Get-ScheduledTask -TaskName $legacySystemTaskName -ErrorAction SilentlyContinue
if ($legacyTask) {
  Unregister-ScheduledTask -TaskName $legacySystemTaskName -Confirm:$false -ErrorAction SilentlyContinue
}

if ($NoAutoStart) {
  foreach ($taskName in @($SystemTaskName, $legacySystemTaskName)) {
    $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existingTask) {
      Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    }
  }
} else {
  & icacls $inventoryRoot /grant "SYSTEM:(OI)(CI)(M)" "Users:(OI)(CI)(M)" /T /C | Out-Null

  $taskActionArgs = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$supervisorPath`""
  $taskAction = New-ScheduledTaskAction -Execute $powershellExe -Argument $taskActionArgs -WorkingDirectory $InstallDir
  $taskTrigger = New-ScheduledTaskTrigger -AtStartup
  $taskPrincipal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
  $taskSettings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew

  Register-ScheduledTask `
    -TaskName $SystemTaskName `
    -Action $taskAction `
    -Trigger $taskTrigger `
    -Principal $taskPrincipal `
    -Settings $taskSettings `
    -Description "Keeps Inventory running before login and restarts after crashes." `
    -Force | Out-Null

  try {
    Start-ScheduledTask -TaskName $SystemTaskName
  } catch {
  }
}

if ($RunAfterInstall) {
  Start-Process -FilePath $launcherPath
}

Write-Host "Installed Inventory to: $InstallDir"
Write-Host "Data directory: $DataDir"
Write-Host "Brand name: $BrandName"
if ($brandLogoRef) {
  Write-Host "Brand logo ref: $brandLogoRef"
}
Write-Host "Start Menu shortcut: $startShortcutPath"
Write-Host "Manual Start shortcut: $manualShortcutPath"
Write-Host "Stop shortcut: $stopShortcutPath"
Write-Host "Uninstall shortcut: $uninstallShortcutPath"
if (-not $NoDesktopShortcut) {
  Write-Host "Desktop shortcut: $desktopShortcutPath"
}
if (-not $NoAutoStart) {
  Write-Host "System startup task: $SystemTaskName"
  Write-Host "Startup mode: before login (SYSTEM)"
  Write-Host "Crash restart supervision: enabled (Task Scheduler + watchdog)"
} else {
  Write-Host "System startup task: disabled"
}
Write-Host "Firewall rule: $FirewallRuleName (TCP $Port, Private/Public profiles)"
