# Stingray Inventory Desktop (Windows)

This desktop app runs the Stingray inventory system on a PC while keeping the same core data formats used on the ESP32/SD build.

## Backward Compatibility

The desktop app reads and writes:

- `inventory.csv` (pipe-delimited Stingray schema)
- `orders.json`
- `transactions.csv`
- `device_log.csv`
- `time_log.csv`
- `/images/*` assets referenced by `image_ref`

It supports legacy inventory rows the firmware already supports (`id|name|qty|updated_at` and older intermediate formats).

## Quick Start

From PowerShell in this `desktop_app` folder:

```powershell
.\install_and_run.ps1
```

Default URL:

- `http://127.0.0.1:8787`

Default data directory:

- `%USERPROFILE%\StingrayInventoryDesktop\data`

## Using Existing SD Data

1. Copy the SD card contents into a folder on the PC.
2. Launch with that folder as `--data-dir` (or `-DataDir` in `install_and_run.ps1`).

Example:

```powershell
.\install_and_run.ps1 -DataDir "D:\StingraySDBackup"
```

## Dev Run (without helper script)

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe .\stingray_desktop_app.py --open-browser
```

## Build Installer (Windows)

Build standalone EXE + installer bundle:

```powershell
.\build_installer.ps1
```

Build and install immediately on this PC:

```powershell
.\build_installer.ps1 -InstallNow
```

`-InstallNow` must be run in Administrator PowerShell.

Manual install from built artifacts:

```powershell
.\install_desktop_app.ps1
```

One-click install from the generated ZIP:

- Unzip `dist\StingrayInventoryDesktop-Installer.zip`
- Double-click `Install Stingray Inventory Desktop.cmd`
- Approve the Windows UAC prompt

You can override branding during install:

```powershell
.\install_desktop_app.ps1 -BrandName "Stingray Airsoft" -BrandLogoPath "C:\Users\TALLEY\Pictures\stingray logo.png"
```

By default, install creates:

- Install directory: `C:\Program Files\StingrayInventoryDesktop`
- Data directory: `C:\ProgramData\StingrayInventoryDesktop\data`
- Start Menu shortcut: `Stingray Inventory Desktop`
- Desktop shortcut: `Stingray Inventory Desktop`
- SYSTEM startup task: `Stingray Inventory Desktop (System Startup)` (starts before login)

Important:

- Installer auto-elevates and requests administrator approval using UAC.
- The startup task runs as `SYSTEM` and keeps the app alive (auto restart on crash).

Installer zip output:

- `dist\StingrayInventoryDesktop-Installer.zip`

## Smoke Test

```powershell
.\.venv\Scripts\python.exe .\stingray_desktop_app.py --self-test
```
