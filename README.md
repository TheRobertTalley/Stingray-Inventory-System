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
- Search by part name, category, part number, QR code, color, material, or BOM fields
- Export filtered inventory CSV by category
- Open per-item page links (`/item?id=<part_number>`) for QR labels
- On per-item page: add/subtract with `+1`, `-1`, or custom number
- Set exact quantity directly
- Highlight zero-stock rows in red on the main page
- Store a simple BOM:
  - parts can point to a `bom_product`
  - `bom_qty` is the quantity of that part used in the product or kit
  - products and kits automatically show their BOM components on the item page

## Repository Layout

- `firmware/StingrayInventoryESP32/StingrayInventoryESP32.ino`: complete ESP32 firmware
- `platformio.ini`: optional PlatformIO environment
- `LICENSE`
- `README.md`

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

`part_number` is the primary identifier. The app still uses `/item?id=<part_number>` in URLs, but there is no separate `id` column in inventory export. `qr_code` is the stored QR or UPC value. `qr_link` is derived by the ESP32 for label use and export. A part belongs to a product or kit when its `bom_product` matches that product or kit part number.

## Notes

- This design intentionally avoids cloud dependencies.
- Data persists on SD card, not in volatile memory.
- Keep writes simple and reliable for small to moderate inventory sizes.
