param(
  [string]$SourceDir = "",
  [string]$InstallDir = "$env:ProgramFiles\\StingrayInventoryDesktop",
  [string]$DataDir = "$env:ProgramData\\StingrayInventoryDesktop\\data",
  [int]$Port = 8787,
  [string]$BrandName = "Stingray Airsoft",
  [string]$BrandLogoPath = "",
  [switch]$NoDesktopShortcut,
  [switch]$NoAutoStart,
  [string]$SystemTaskName = "Stingray Inventory Desktop (System Startup)",
  [switch]$RunAfterInstall
)

$ErrorActionPreference = "Stop"

$cloudHeader = "provider|login_email|folder_name|folder_hint|mode|backup_mode|asset_mode|brand_name|brand_logo_ref|client_id|client_secret|updated_at"
$defaultBrandLogoPath = "C:\\Users\\TALLEY\\Pictures\\stingray logo.png"

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

  if (Test-Path $defaultBrandLogoPath) {
    return (Resolve-Path $defaultBrandLogoPath).Path
  }

  return ""
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
    "-SystemTaskName", $SystemTaskName
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
New-Item -ItemType Directory -Force -Path (Join-Path $DataDir "images") | Out-Null

Get-ChildItem -LiteralPath $InstallDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $InstallDir -Recurse -Force

$mainPageUrl = "http://127.0.0.1:$Port/"
$launcherPath = Join-Path $InstallDir "Run-StingrayDesktop.cmd"
$launcherContent = @"
@echo off
setlocal
set APPDIR=%~dp0
set URL=$mainPageUrl
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Invoke-WebRequest -Uri '%URL%api/status' -UseBasicParsing -TimeoutSec 2 | Out-Null; Start-Process '%URL%'; exit 0 } catch { exit 1 }"
if %ERRORLEVEL% EQU 0 goto :done
"%APPDIR%StingrayInventoryDesktop.exe" --host 127.0.0.1 --port $Port --data-dir "$DataDir" --open-browser
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
$args = @("--host", "127.0.0.1", "--port", "__PORT__", "--data-dir", "__DATADIR__")

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

$cloudConfigPath = Join-Path $DataDir "cloud_backup.cfg"
Update-CloudBranding -CloudConfigPath $cloudConfigPath -NewBrandName $BrandName -NewBrandLogoRef $brandLogoRef

$shortcutIcon = if (Test-Path $iconPath) { $iconPath } else { Join-Path $InstallDir "StingrayInventoryDesktop.exe" }

$wsh = New-Object -ComObject WScript.Shell
$startMenuDir = Join-Path $env:ProgramData "Microsoft\\Windows\\Start Menu\\Programs\\Stingray Inventory Desktop"
New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null

$startShortcutPath = Join-Path $startMenuDir "Stingray Inventory Desktop.lnk"
$startShortcut = $wsh.CreateShortcut($startShortcutPath)
$startShortcut.TargetPath = $launcherPath
$startShortcut.WorkingDirectory = $InstallDir
$startShortcut.IconLocation = $shortcutIcon
$startShortcut.Description = "Open Stingray Inventory main page"
$startShortcut.Save()

$manualShortcutPath = Join-Path $startMenuDir "Stingray Inventory Desktop (Manual Start).lnk"
$manualShortcut = $wsh.CreateShortcut($manualShortcutPath)
$manualShortcut.TargetPath = $launcherPath
$manualShortcut.WorkingDirectory = $InstallDir
$manualShortcut.IconLocation = $shortcutIcon
$manualShortcut.Description = "Start Stingray Inventory desktop app and open browser"
$manualShortcut.Save()

$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "Stingray Inventory Desktop.lnk"
if (-not $NoDesktopShortcut) {
  $desktopShortcut = $wsh.CreateShortcut($desktopShortcutPath)
  $desktopShortcut.TargetPath = $launcherPath
  $desktopShortcut.WorkingDirectory = $InstallDir
  $desktopShortcut.IconLocation = $shortcutIcon
  $desktopShortcut.Description = "Open Stingray Inventory main page"
  $desktopShortcut.Save()
}

$powershellExe = Resolve-PowerShellExe
$legacyStartupShortcutPaths = @(
  (Join-Path ([Environment]::GetFolderPath("Startup")) "Stingray Inventory Desktop Background.lnk"),
  (Join-Path ([Environment]::GetFolderPath("CommonStartup")) "Stingray Inventory Desktop Background.lnk")
)
foreach ($legacyPath in $legacyStartupShortcutPaths) {
  if (Test-Path $legacyPath) {
    Remove-Item -LiteralPath $legacyPath -Force -ErrorAction SilentlyContinue
  }
}

if ($NoAutoStart) {
  $existingTask = Get-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue
  if ($existingTask) {
    Unregister-ScheduledTask -TaskName $SystemTaskName -Confirm:$false
  }
} else {
  & icacls $DataDir /grant "SYSTEM:(OI)(CI)(M)" "Users:(OI)(CI)(M)" /T /C | Out-Null

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
    -Description "Keeps Stingray Inventory Desktop running before login and restarts after crashes." `
    -Force | Out-Null

  try {
    Start-ScheduledTask -TaskName $SystemTaskName
  } catch {
  }
}

if ($RunAfterInstall) {
  Start-Process -FilePath $launcherPath
}

Write-Host "Installed Stingray Inventory Desktop to: $InstallDir"
Write-Host "Data directory: $DataDir"
Write-Host "Brand name: $BrandName"
if ($brandLogoRef) {
  Write-Host "Brand logo ref: $brandLogoRef"
}
Write-Host "Start Menu shortcut: $startShortcutPath"
Write-Host "Manual Start shortcut: $manualShortcutPath"
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
