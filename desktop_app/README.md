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

Default local URL:

- `http://127.0.0.1:8787`

Default LAN behavior:

- The desktop app listens on `0.0.0.0:8787`.
- The Settings page shows the LAN URL, for example `http://192.168.1.50:8787`.
- QR links and CSV exports use the configured LAN base URL, not `localhost`.

Default data directory:

- `C:\ProgramData\StingrayInventoryDesktop\data`

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
.\.venv\Scripts\python.exe .\stingray_desktop_app.py --host 0.0.0.0 --port 8787 --open-browser
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
- Start Menu shortcut: `Stop Stingray Inventory Desktop`
- Start Menu shortcut: `Uninstall Stingray Inventory Desktop`
- Desktop shortcut: `Stingray Inventory Desktop`
- SYSTEM startup task: `Stingray Inventory Desktop (System Startup)` (starts before login)
- Windows Firewall rule: `Stingray Inventory Desktop LAN` for TCP port `8787` on Private and Public networks

Important:

- Installer auto-elevates and requests administrator approval using UAC.
- The startup task runs as `SYSTEM` and keeps the app alive (auto restart on crash).
- Settings includes an **Auto run and crash restart** checkbox for turning that startup task on or off.
- Updates replace app files only. Inventory data remains in `C:\ProgramData\StingrayInventoryDesktop\data`.

## LAN / QR Setup

1. Open `http://127.0.0.1:8787/settings` on the inventory PC.
2. In **Desktop LAN Access**, select the correct LAN IP, usually `192.168.x.x`.
3. Confirm the QR/base URL is `http://<LAN-IP>:8787`.
4. Click **Save LAN URL**.
5. Use **Copy LAN URL** or **Network Test** to tell another device what URL to try.

TP-Link checks if phones cannot connect:

- Phone must be on the same WiFi.
- Guest WiFi isolation must be off.
- TP-Link AP/client isolation must be off.
- Windows Firewall rule must exist for TCP `8787` on the active network profile.

## SD Card Import

On the Settings page, use **Import ESP32 SD Data**:

- Enter the SD card drive or copied SD folder path.
- Preview first.
- Choose merge or replace-after-backup.
- Import always creates a backup first in `C:\ProgramData\StingrayInventoryDesktop\backups`.

Supported SD files include `inventory.csv`, `transactions.csv`, `orders.json`, logs, config files, and `images/`.

## Backup ZIP Export / Import

On the Settings page:

- **Backup Current Data** creates and downloads a ZIP backup.
- **Import Backup ZIP** restores a ZIP backup.
- Backup ZIPs are stored in `C:\ProgramData\StingrayInventoryDesktop\backups`.

The app creates an automatic ZIP backup before SD import, backup restore, item delete, image removal, and image replacement.
Portable backups do not include per-PC LAN settings, so restoring on another computer picks up that computer's network settings instead of the original one.

## Item Editing

Open an item page and use the added **Edit Item** section to update item fields or upload/remove the item image. Existing quantity controls remain at the top of the original item page.

## QR Labels

Open:

```text
http://127.0.0.1:8787/labels
```

The label page prints QR labels using the configured LAN base URL. Filter by category or search before printing.

## Barcode Scanner Workflow

USB barcode scanners that type like a keyboard work in the main inventory search box:

- The search box auto-focuses on page load.
- Scan or type a part number and press Enter.
- If the scan exactly matches a part number, QR code, or QR link, the item page opens.

Installer zip output:

- `dist\StingrayInventoryDesktop-Installer.zip`

## Stop / Disable For Development

Use either:

- Start Menu: `Stop Stingray Inventory Desktop`
- Or in the install folder: `stop_desktop_app.cmd`

Default behavior:

- Stops all running app processes
- Stops supervisor process
- Disables system startup task so it stays off

To stop but keep autostart enabled:

```powershell
.\stop_desktop_app.ps1 -KeepAutoStart
```

## Uninstall

Use either:

- Start Menu: `Uninstall Stingray Inventory Desktop`
- Or in the install folder: `uninstall_desktop_app.cmd`

Default behavior:

- Stops app
- Removes startup task and shortcuts
- Removes app files
- Keeps data at `C:\ProgramData\StingrayInventoryDesktop\data`

To also remove data:

```powershell
.\uninstall_desktop_app.ps1 -RemoveData
```

## Smoke Test

```powershell
.\.venv\Scripts\python.exe .\stingray_desktop_app.py --self-test
```
