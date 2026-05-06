# Stingray Inventory System (ESP32 + SD)

Stingray Inventory System is now an ESP32-hosted inventory app.

- Runs directly on an ESP32 on your local network
- Stores inventory on a microSD card
- Serves web pages from the ESP32 itself
- Uses QR codes that point to per-item pages on the ESP32

## What You Can Do

- Add items from the main page
- Remove items from the main page
- View inventory by tab:
  - all inventory
  - parts
  - products
  - kits
- Search by name, category, part number, QR/UPC, color, material, or parent product/kit fields
- Export filtered inventory CSV by category
- Open per-item page links (`/item?id=<part_number>`) for QR labels
- On per-item page: add/subtract with `+1`, `-1`, or custom number
- Set exact quantity directly
- Highlight zero-stock rows in red on the main page
- Store a simple BOM:
  - parts can point to a parent product or kit with `bom_product`
  - `bom_qty` is the quantity of that part used in the parent product or kit
  - products and kits automatically show their BOM components on the item page

## Repository Layout

- `firmware/StingrayInventoryESP32/StingrayInventoryESP32.ino`: complete ESP32 firmware
- `desktop_app/`: native PC-hosted Stingray desktop service (same inventory file compatibility)
- `platformio.ini`: optional PlatformIO environment
- `LICENSE`
- `README.md`

## Desktop PC Migration (New)

You can now run Stingray Inventory directly on Windows while preserving existing SD data formats.

The Windows desktop app is LAN-first: it listens on `0.0.0.0:8787`, remains available on the PC at `http://127.0.0.1:8787`, and lets Settings choose the LAN base URL used by QR codes and exports.
Backups are portable between computers and restore inventory, orders, logs, and images without carrying over the old PC's LAN settings.

- Desktop entry point: `desktop_app/stingray_desktop_app.py`
- One-command setup/run on Windows: `desktop_app/install_and_run.ps1`
- Build standalone desktop installer bundle: `desktop_app/build_installer.ps1`
- Install built desktop app on a PC: `desktop_app/install_desktop_app.ps1`
- Stop desktop app + disable autostart: `desktop_app/stop_desktop_app.ps1`
- Uninstall desktop app: `desktop_app/uninstall_desktop_app.ps1`
- Desktop notes: `desktop_app/README.md`
- Desktop installer now supports Inventory app naming, Stingray branding/logo, desktop shortcut, first-run setup wizard, health dashboard, admin PIN gate, system startup task with crash restart, LAN firewall access for TCP `8787`, and a Settings toggle for autorun.
- Upgrades cleanly remove old desktop app binaries and legacy shortcuts, while preserving and importing existing inventory data from older `ProgramData` trees.
- Installer ZIP includes one-click setup (`Install Inventory.cmd`) that prompts for UAC and configures startup correctly.

## Hardware

Current development target:

- `ESP32-2432S028` stand-in board
- 8 GB FAT32 microSD card

Planned production target:

- `LILYGO T-Dongle-S3`
- onboard TF / microSD slot

This firmware is still web-first. The inventory UI is served over WiFi, so neither board display is used yet.

Board-specific storage wiring:

- `esp32-2432s028` PlatformIO env uses SPI SD with `CS=5`, `SCK=18`, `MISO=19`, `MOSI=23`
- `t-dongle-s3` PlatformIO env uses `SD_MMC` with `CLK=39`, `CMD=38`, `D0=40`, `D1=41`, `D2=42`, `D3=47`

If you build outside PlatformIO, the sketch falls back to the SPI SD mapping above.

## Software Setup (Arduino IDE)

1. Install Arduino IDE.
2. Install ESP32 board package.
3. Open `firmware/StingrayInventoryESP32/StingrayInventoryESP32.ino`.
4. Set these values near the top of the sketch:
   - `WIFI_SSID`
   - `WIFI_PASSWORD`
   - optional `HOSTNAME` (default `stingray`)
5. Select your ESP32 board and COM port.
6. Upload.

## Optional: PlatformIO

`platformio.ini` includes both the temporary and final board targets. The default environment is the current stand-in board.

Build for the current stand-in board:

```bash
pio run -e esp32-2432s028
pio run -e esp32-2432s028 --target upload
```

Build for the final T-Dongle-S3 target:

```bash
pio run -e t-dongle-s3
pio run -e t-dongle-s3 --target upload
```

## Auto-Publish To Web Flasher

This repo now includes GitHub Actions automation at:

- `.github/workflows/publish-web-flasher.yml`

Behavior:

- On every push to `main`, CI builds `t-dongle-s3` firmware binaries.
- CI also builds the Windows desktop installer zip (`StingrayInventoryDesktop-Installer.zip`).
- It updates `projects/stingray-inventory-system/` and `projects/catalog.json` in:
  - `https://github.com/TheRobertTalley/Stingray-Web-Flasher`
- That automatically keeps:
  - `https://theroberttalley.github.io/Stingray-Web-Flasher/`
  current with the latest firmware build and desktop installer download card.

Required one-time setup in this repo (`Settings -> Secrets and variables -> Actions`):

- Add secret `STINGRAY_WEB_FLASHER_TOKEN`
- Use a GitHub token that has write access to:
  - `TheRobertTalley/Stingray-Web-Flasher`

## First Boot Behavior

On startup, the firmware:

- connects to WiFi
- initializes SD card
- creates SD files if missing:
  - `/inventory.csv`
  - `/transactions.csv`
- starts HTTP server on port 80

If WiFi fails, it falls back to AP mode (`Stingray-Inventory`).

## Access

Use one of:

- `http://stingray.local/` (if mDNS works)
- `http://<esp32-ip>/`

## QR Code Format

For each part number, encode this URL in the QR label:

- `http://stingray.local/item?id=1001`

or using IP:

- `http://<esp32-ip>/item?id=1001`

When scanned, the phone opens that item page hosted on the ESP32, where quantity can be adjusted immediately.

## API Endpoints (used by UI)

- `GET /api/status`
- `GET /api/items`
- `GET /api/item?id=<part_number>`
- `POST /api/items/add`
- `POST /api/items/remove`
- `POST /api/items/adjust`
- `POST /api/items/set`
- `GET /api/export`

## SD Storage Format

Inventory file (`/inventory.csv`, pipe-delimited):

- `part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at`

Transaction log (`/transactions.csv`):

- `timestamp|item_id|action|delta|qty_after|note`

The firmware still reads the older legacy inventory format:

- `id|name|qty|updated_at`

## Spreadsheet Compatibility

The ESP32 inventory schema and spreadsheet export now use the same flat field set:

- `part_number`
- `category`
- `part_name`
- `qr_code`
- `color`
- `material`
- `qty`
- `image_ref`
- `bom_product`
- `bom_qty`
- `updated_at`
- `qr_link`

`part_number` is the primary identifier. The app still uses `/item?id=<part_number>` in URLs, but there is no separate `id` column in inventory export. `part_name` stores the display name for parts, products, and kits. `qr_code` is the stored QR or UPC value. `qr_link` is derived by the ESP32 for label use and export. A part belongs to a parent product or kit when its `bom_product` matches that product or kit part number.

## Notes

- This design intentionally avoids cloud dependencies.
- Data persists on SD card, not in volatile memory.
- Keep writes simple and reliable for small to moderate inventory sizes.
