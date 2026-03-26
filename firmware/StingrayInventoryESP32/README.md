# Firmware Notes

## File

- `StingrayInventoryESP32.ino`: full firmware (WiFi + SD + HTTP UI/API)

## Config Values

Edit at top of sketch:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `HOSTNAME`

Storage mode and SD pin mapping now come from the selected PlatformIO environment:

- `esp32-2432s028`: SPI SD on GPIO `18/19/23/5`
- `t-dongle-s3`: `SD_MMC` on GPIO `39/38/40/41/42/47`

If you compile the sketch outside PlatformIO, it falls back to the SPI SD mapping above.

## SD Files

Created automatically if missing:

- `/inventory.csv`
- `/transactions.csv`

## QR Usage

Use per-item URL in your printed QR code:

- `http://<esp32-host>/item?id=<item_id>`

Example:

- `http://stingray.local/item?id=1001`
