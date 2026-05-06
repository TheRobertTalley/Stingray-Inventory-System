param(
  [string]$InstallDir = "$env:ProgramFiles\\Inventory",
  [string]$SystemTaskName = "Inventory (System Startup)",
  [switch]$KeepAutoStart
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
    "-SystemTaskName", $SystemTaskName
  )
  if ($KeepAutoStart) { $elevatedArgs += "-KeepAutoStart" }

  try {
    $proc = Start-Process -FilePath $powershellExe -Verb RunAs -ArgumentList $elevatedArgs -PassThru -Wait
    exit $proc.ExitCode
  } catch {
    throw "Administrator approval is required to stop or disable the system startup task."
  }
}

$task = Get-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue
if ($task) {
  try {
    Stop-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue
  } catch {
  }
  if (-not $KeepAutoStart) {
    Disable-ScheduledTask -TaskName $SystemTaskName -ErrorAction SilentlyContinue | Out-Null
  }
}

$legacyTaskName = "Stingray Inventory Desktop (System Startup)"
if (-not $task) {
  $legacyTask = Get-ScheduledTask -TaskName $legacyTaskName -ErrorAction SilentlyContinue
  if ($legacyTask) {
    try {
      Stop-ScheduledTask -TaskName $legacyTaskName -ErrorAction SilentlyContinue
    } catch {
    }
    if (-not $KeepAutoStart) {
      Disable-ScheduledTask -TaskName $legacyTaskName -ErrorAction SilentlyContinue | Out-Null
    }
  }
}

# Stop app processes.
$appProcesses = Get-Process -Name "StingrayInventoryDesktop" -ErrorAction SilentlyContinue
if ($appProcesses) {
  $appProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
}

# Stop supervisor PowerShell processes if they are running this app's supervisor script.
$supervisorScript = Join-Path $InstallDir "StingrayDesktopSupervisor.ps1"
$escapedSupervisor = [Regex]::Escape($supervisorScript)
$psProcesses = Get-CimInstance Win32_Process -Filter "name='powershell.exe'" -ErrorAction SilentlyContinue
foreach ($proc in ($psProcesses | Where-Object { $_.CommandLine -match $escapedSupervisor })) {
  try {
    Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
  } catch {
  }
}

if (-not $KeepAutoStart) {
  Write-Host "Inventory stopped and startup task disabled."
} else {
  Write-Host "Inventory stopped. Startup task remains enabled."
}
