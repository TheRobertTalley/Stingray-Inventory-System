# Inventory Desktop (Windows)

This desktop app runs the Inventory system on a PC while keeping the same core data formats used on the ESP32/SD build.

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
- Ethernet or any normal LAN connection is fine. There is no Wi-Fi scanning workflow on the Windows desktop build.
- QR links and CSV exports use the configured LAN base URL, not `localhost`.

Default data directory:

- `C:\ProgramData\Inventory\data`

Legacy data migration:

- Existing installs from the older Stingray desktop build are detected and copied forward from the legacy `C:\ProgramData\StingrayInventoryDesktop\` tree the first time the new app starts against an empty `Inventory` data tree.

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
- Double-click `Install Inventory.cmd`
- Approve the Windows UAC prompt
- On first launch, the browser opens the first-run setup wizard after the LAN URL is reachable.
- The installer removes old app binaries and legacy shortcuts first, then preserves and imports existing inventory data from older `ProgramData` trees.

You can override branding during install:

```powershell
.\install_desktop_app.ps1 -BrandName "Inventory" -BrandLogoPath "C:\Path\To\Your\Logo.png"
```

If no custom logo is supplied, the bundled `desktop_app/branding/stingray-logo.png` is used automatically.

By default, install creates:

- Install directory: `C:\Program Files\Inventory`
- Data directory: `C:\ProgramData\Inventory\data`
- Logs directory: `C:\ProgramData\Inventory\logs`
- Config directory: `C:\ProgramData\Inventory\config`
- Start Menu shortcut: `Inventory`
- Start Menu shortcut: `Stop Inventory`
- Start Menu shortcut: `Uninstall Inventory`
- Desktop shortcut: `Inventory`
- SYSTEM startup task: `Inventory (System Startup)` (starts before login)
- Windows Firewall rule: `Inventory LAN` for TCP port `8787` on Private and Public networks
- First-run setup wizard: choose the LAN URL and create the admin PIN
- Health dashboard: green/red status plus `Copy LAN URL` and `Test This PC`
- Admin PIN gate: advanced settings stay locked until the PIN is unlocked
- Legacy installs: old app files and shortcuts are removed before the new install is copied in, while inventory data is preserved and imported when found.

Important:

- Installer auto-elevates and requests administrator approval using UAC.
- The startup task runs as `SYSTEM` and keeps the app alive (auto restart on crash).
- Settings includes an **Auto run and crash restart** checkbox for turning that startup task on or off.
- The installer grants the desktop app write access to `C:\ProgramData\Inventory\` so config, logs, backups, and data all stay writable after install.
- Updates replace app files only. Inventory data remains in `C:\ProgramData\Inventory\data`, and legacy inventory files are imported when present.

## LAN / QR Setup

1. Open the first-run wizard at `http://127.0.0.1:8787/setup` on the inventory PC.
2. Pick the correct LAN IP, usually `192.168.x.x`.
3. Confirm the QR/base URL is `http://<LAN-IP>:8787`.
4. Create the admin PIN and finish setup.
5. In Settings, use **Copy LAN URL** or **Test This PC** to verify the LAN URL and health dashboard.

TP-Link checks if phones cannot connect:

- Phone or tablet must be on the same LAN.
- Guest isolation must be off.
- AP/client isolation must be off.
- Windows Firewall rule must exist for TCP `8787` on the active network profile.

## Inventory Folder Import

Use the dedicated **Import Inventory Folder** page first:

- Open **Views And Tools** -> **Import Inventory Folder**, or go straight to `/import-folder`.
- Unlock admin access if the page asks for a PIN.

Then use **Import Inventory Folder**:

- Enter a copied inventory folder path, such as `C:\Users\TALLEY\Desktop\old inventory`.
- You can also point it at a copied SD-style folder from an older Stingray export.
- Or click **Choose Folder...** and pick the folder in Windows, or drag the folder onto the drop zone.
- Preview first.
- Choose merge or replace-after-backup.
- Import always creates a backup first in `C:\ProgramData\Inventory\backups`.

Supported files include `inventory.csv`, `transactions.csv`, `orders.json`, logs, config files, and `images/`.

## Backup ZIP Export / Import

After unlocking admin settings:

- **Backup Current Data** creates and downloads a ZIP backup.
- **Import Backup ZIP** restores a ZIP backup.
- Backup ZIPs are stored in `C:\ProgramData\Inventory\backups`.

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

- Start Menu: `Stop Inventory`
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

- Start Menu: `Uninstall Inventory`
- Or in the install folder: `uninstall_desktop_app.cmd`

Default behavior:

- Stops app
- Removes startup task and shortcuts
- Removes app files
- Keeps data at `C:\ProgramData\Inventory\data`
- Also removes legacy Program Files app folders and legacy shortcuts from the old Stingray install name.

To also remove data:

```powershell
.\uninstall_desktop_app.ps1 -RemoveData
```

That purge removes the Inventory data, backups, logs, and config folders under `C:\ProgramData\Inventory` and the older `C:\ProgramData\StingrayInventoryDesktop` tree if it still exists.

## Smoke Test

```powershell
.\.venv\Scripts\python.exe .\stingray_desktop_app.py --self-test
```
