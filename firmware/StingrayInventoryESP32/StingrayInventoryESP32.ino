#include <FS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_system.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <qrcode.h>
#include <algorithm>
#include <time.h>
#include <vector>

#if defined(ARDUINO_DONGLES3)
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#endif

#if defined(STINGRAY_USE_SD_MMC)
#include <SD_MMC.h>
#else
#include <SD.h>
#endif

#ifndef STINGRAY_BOARD_NAME
#define STINGRAY_BOARD_NAME "GENERIC_ESP32"
#endif

#ifndef STINGRAY_SD_SPI_CS
#define STINGRAY_SD_SPI_CS 5
#endif

#ifndef STINGRAY_SD_SPI_SCK
#define STINGRAY_SD_SPI_SCK 18
#endif

#ifndef STINGRAY_SD_SPI_MISO
#define STINGRAY_SD_SPI_MISO 19
#endif

#ifndef STINGRAY_SD_SPI_MOSI
#define STINGRAY_SD_SPI_MOSI 23
#endif

#ifndef STINGRAY_SD_MMC_CLK
#define STINGRAY_SD_MMC_CLK 39
#endif

#ifndef STINGRAY_SD_MMC_CMD
#define STINGRAY_SD_MMC_CMD 38
#endif

#ifndef STINGRAY_SD_MMC_D0
#define STINGRAY_SD_MMC_D0 40
#endif

#ifndef STINGRAY_SD_MMC_D1
#define STINGRAY_SD_MMC_D1 41
#endif

#ifndef STINGRAY_SD_MMC_D2
#define STINGRAY_SD_MMC_D2 42
#endif

#ifndef STINGRAY_SD_MMC_D3
#define STINGRAY_SD_MMC_D3 47
#endif

#ifndef STINGRAY_DONGLE_LCD_MOSI
#define STINGRAY_DONGLE_LCD_MOSI 3
#endif

#ifndef STINGRAY_DONGLE_LCD_CLK
#define STINGRAY_DONGLE_LCD_CLK 5
#endif

#ifndef STINGRAY_DONGLE_LCD_CS
#define STINGRAY_DONGLE_LCD_CS 4
#endif

#ifndef STINGRAY_DONGLE_LCD_DC
#define STINGRAY_DONGLE_LCD_DC 2
#endif

#ifndef STINGRAY_DONGLE_LCD_RST
#define STINGRAY_DONGLE_LCD_RST 1
#endif

#ifndef STINGRAY_DONGLE_LCD_BCKL
#define STINGRAY_DONGLE_LCD_BCKL 38
#endif

#ifndef STINGRAY_DONGLE_LED_DI
#define STINGRAY_DONGLE_LED_DI 40
#endif

#ifndef STINGRAY_DONGLE_LED_CI
#define STINGRAY_DONGLE_LED_CI 39
#endif

// -------------------------------
// User configuration
// -------------------------------
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* HOSTNAME = "stingray";
const char* BOARD_NAME = STINGRAY_BOARD_NAME;

#if defined(STINGRAY_USE_SD_MMC)
const char* STORAGE_MODE = "sd_mmc";
#else
const char* STORAGE_MODE = "spi_sd";
#endif

const char* INVENTORY_FILE = "/inventory.csv";
const char* INVENTORY_TMP_FILE = "/inventory.tmp";
const char* ORDERS_FILE = "/orders.json";
const char* ORDERS_TMP_FILE = "/orders.tmp";
const char* TRANSACTION_FILE = "/transactions.csv";
const char* DEVICE_LOG_FILE = "/device_log.csv";
const char* TIME_LOG_FILE = "/time_log.csv";
const char* IMAGE_DIR = "/images";
const char* UI_DIR = "/ui";
const char* UI_INDEX_FILE = "/ui/index.html";
const char* UI_ITEM_FILE = "/ui/item.html";
const char* UI_VERSION_FILE = "/ui/version.txt";
const char* CLOUD_CONFIG_FILE = "/cloud_backup.cfg";
const char* CLOUD_CONFIG_TMP_FILE = "/cloud_backup.tmp";
const char* GOOGLE_STATE_FILE = "/google_drive_state.cfg";
const char* GOOGLE_STATE_TMP_FILE = "/google_drive_state.tmp";
const char* PREFS_NAMESPACE = "stingrayinv";
const char* PREFS_CLOUD_CONFIG_KEY = "cloud_cfg";
const char* PREFS_GOOGLE_STATE_KEY = "g_state";
const char* PREFS_WIFI_SSID_KEY = "wifi_ssid";
const char* PREFS_WIFI_PASS_KEY = "wifi_pass";
const char* PREFS_WIFI_UPDATED_KEY = "wifi_upd";
const char* PREFS_SD_BASELINE_KEY = "sd_base_v1";
const size_t MAX_IMAGE_UPLOAD_BYTES = 2 * 1024 * 1024;
const size_t MAX_ORDERS_PAYLOAD_BYTES = 256 * 1024;
const size_t MAX_ORDER_FULFILL_PLAN_BYTES = 8192;
const size_t MAX_ORDER_FULFILL_LINES = 200;
const char* UI_ASSET_VERSION = "2026-03-26-ui-order-fulfill-1";
const char* GOOGLE_DRIVE_SCOPE = "https://www.googleapis.com/auth/drive.file";
const char* GOOGLE_DEVICE_CODE_URL = "https://oauth2.googleapis.com/device/code";
const char* GOOGLE_TOKEN_URL = "https://oauth2.googleapis.com/token";
const char* GOOGLE_DRIVE_FILES_URL = "https://www.googleapis.com/drive/v3/files";
const char* GOOGLE_DRIVE_UPLOAD_URL = "https://www.googleapis.com/upload/drive/v3/files";
const char* GOOGLE_MANIFEST_NAME = "stingray_manifest.txt";
const char* WIFI_FALLBACK_AP_NAME = "Stingray-Inventory";

WebServer server(80);

struct ItemRecord {
  String id;
  String category;
  String partName;
  String qrCode;
  String color;
  String material;
  int32_t qty;
  String imageRef;
  String bomProduct;
  int32_t bomQty;
  String updatedAt;
};

struct CloudBackupConfig {
  String provider;
  String loginEmail;
  String folderName;
  String folderHint;
  String mode;
  String backupMode;
  String assetMode;
  String brandName;
  String brandLogoRef;
  String clientId;
  String clientSecret;
  String updatedAt;
};

struct GoogleDriveState {
  String refreshToken;
  String folderId;
  String lastSyncAt;
  String lastSyncedManifestHash;
  String lastSyncedSnapshotAt;
  String localSnapshotAt;
  String authStatus;
  String syncStatus;
  String lastError;
  String accessToken;
  String tokenType;
  String scope;
  String deviceCode;
  String userCode;
  String verificationUrl;
  uint32_t devicePollIntervalSeconds;
  uint32_t accessTokenExpiresAt;
};

struct WifiConfig {
  String ssid;
  String password;
  String updatedAt;
};

struct BackupFileEntry {
  String path;
  String mimeType;
  size_t size;
  String hash;
  String remoteName;
};

struct BackupManifest {
  String snapshotAt;
  String manifestHash;
  std::vector<BackupFileEntry> entries;
};

struct OrderFulfillmentEntry {
  String itemId;
  int32_t needed;
};

std::vector<ItemRecord> g_items;
bool g_sdReady = false;
String g_baseUrl = "http://0.0.0.0";
const char* DEFAULT_CATEGORY = "part";
CloudBackupConfig g_cloudBackupConfig;
GoogleDriveState g_googleDriveState;
WifiConfig g_wifiConfig;
File g_uploadFile;
String g_uploadStoredPath = "";
String g_uploadError = "";
size_t g_uploadBytesWritten = 0;
String g_deviceId = "";
String g_macAddress = "";
String g_resetReason = "";
String g_timeSource = "uptime";
Preferences g_preferences;
bool g_preferencesReady = false;
bool g_inventoryLoadHealthy = false;
String g_wifiLastError = "";
bool g_pendingApShutdown = false;
uint32_t g_pendingApShutdownAt = 0;

#if defined(ARDUINO_DONGLES3)
SPIClass g_dongleDisplaySpi(HSPI);
Adafruit_ST7735 g_dongleDisplay(&g_dongleDisplaySpi, STINGRAY_DONGLE_LCD_CS, STINGRAY_DONGLE_LCD_DC, STINGRAY_DONGLE_LCD_RST);
CRGB g_dongleLed[1];
bool g_dongleDisplayReady = false;
bool g_dongleLedReady = false;
uint8_t g_dongleLedHue = 0;
uint32_t g_dongleNextLedUpdateAt = 0;
uint32_t g_dongleNextScreenRefreshAt = 0;
uint32_t g_dongleNotificationExpiresAt = 0;
uint32_t g_dongleNextIdleRefreshAt = 0;
String g_dongleLastEventTitle = "READY";
String g_dongleLastEventLine1 = "Waiting for inventory";
String g_dongleLastEventLine2 = "";
String g_dongleNotificationTitle = "";
String g_dongleNotificationLine1 = "";
String g_dongleNotificationLine2 = "";
uint16_t g_dongleNotificationColor = ST77XX_BLUE;
String g_dongleLastStaticSignature = "";
String g_dongleLastActivitySignature = "";
#endif

String trimCopy(String value) {
  value.trim();
  return value;
}

String normalizeCategory(String value) {
  value = trimCopy(value);
  value.toLowerCase();

  if (value == "parts") {
    return "part";
  }
  if (value == "products") {
    return "product";
  }
  if (value == "kits") {
    return "kit";
  }
  if (value == "part" || value == "product" || value == "kit") {
    return value;
  }

  return String(DEFAULT_CATEGORY);
}

String categoryLabel(const String& category) {
  if (category == "part") {
    return "Part";
  }
  if (category == "product") {
    return "Product";
  }
  if (category == "kit") {
    return "Kit";
  }
  return "Part";
}

String itemDisplayName(const ItemRecord& item) {
  return item.partName;
}

String truncateDisplayText(const String& value, size_t maxLength) {
  if (value.length() <= maxLength) {
    return value;
  }

  if (maxLength <= 3) {
    return value.substring(0, maxLength);
  }

  return value.substring(0, maxLength - 3) + "...";
}

uint16_t boardColor(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t boardBackgroundColor() {
  return boardColor(8, 14, 20);
}

uint16_t boardPanelColor() {
  return boardColor(16, 25, 34);
}

uint16_t boardPanelElevatedColor() {
  return boardColor(23, 35, 47);
}

uint16_t boardBorderColor() {
  return boardColor(36, 57, 74);
}

uint16_t boardTextColor() {
  return boardColor(240, 245, 249);
}

uint16_t boardRowLabelColor() {
  return boardColor(73, 118, 164);
}

int16_t boardTextWidth(const String& text, uint8_t size) {
#if defined(ARDUINO_DONGLES3)
  if (g_dongleDisplayReady) {
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    g_dongleDisplay.setTextSize(size);
    g_dongleDisplay.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
    return static_cast<int16_t>(w);
  }
#endif
  return static_cast<int16_t>(text.length() * 6 * size);
}

int16_t boardTextHeight(uint8_t size) {
  return static_cast<int16_t>(8 * size);
}

String boardConnectionStateLabel() {
  if (WiFi.status() == WL_CONNECTED) {
    return "ONLINE";
  }
  if (wifiApActive()) {
    return "SETUP";
  }
  return "OFFLINE";
}

uint16_t boardConnectionAccentColor() {
  if (WiFi.status() == WL_CONNECTED) {
    return boardColor(45, 183, 123);
  }
  if (wifiApActive()) {
    return boardColor(214, 162, 59);
  }
  return boardColor(91, 120, 149);
}

String currentNetworkDisplayLine() {
  if (WiFi.status() == WL_CONNECTED) {
    return "IP " + WiFi.localIP().toString();
  }

  if (wifiApActive()) {
    return "AP " + WiFi.softAPIP().toString();
  }

  return "Waiting for network";
}

String boardIdleNetworkText() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Connected to " + WiFi.SSID();
  }
  if (wifiApActive()) {
    return String("Join ") + WIFI_FALLBACK_AP_NAME + " to run Wi-Fi setup";
  }
  return "Wi-Fi is offline";
}

String boardIdleAddressText() {
  if (WiFi.status() == WL_CONNECTED) {
    String value = WiFi.localIP().toString();
    if (g_baseUrl.startsWith("http://") && g_baseUrl.indexOf(".local") > 0) {
      value += "  ";
      value += g_baseUrl.substring(7);
    }
    return value;
  }

  if (wifiApActive()) {
    return String(WIFI_FALLBACK_AP_NAME) + "  " + WiFi.softAPIP().toString();
  }

  return g_baseUrl;
}

String boardIdleStatusText() {
#if defined(ARDUINO_DONGLES3)
  String text;
  if (!g_dongleLastEventTitle.isEmpty()) {
    text += g_dongleLastEventTitle;
  }
  if (!g_dongleLastEventLine1.isEmpty()) {
    if (!text.isEmpty()) {
      text += "  ";
    }
    text += g_dongleLastEventLine1;
  }
  if (!g_dongleLastEventLine2.isEmpty()) {
    if (!text.isEmpty()) {
      text += "  ";
    }
    text += g_dongleLastEventLine2;
  }
  return text.isEmpty() ? "Ready for inventory updates" : text;
#else
  return "Ready for inventory updates";
#endif
}

String boardNotificationText() {
#if defined(ARDUINO_DONGLES3)
  String text;
  if (!g_dongleNotificationTitle.isEmpty()) {
    text += g_dongleNotificationTitle;
  }
  if (!g_dongleNotificationLine1.isEmpty()) {
    if (!text.isEmpty()) {
      text += "  ";
    }
    text += g_dongleNotificationLine1;
  }
  if (!g_dongleNotificationLine2.isEmpty()) {
    if (!text.isEmpty()) {
      text += "  ";
    }
    text += g_dongleNotificationLine2;
  }
  return text.isEmpty() ? boardIdleStatusText() : text;
#else
  return boardIdleStatusText();
#endif
}

String boardActivityLabel() {
  return boardNotificationActive() ? "LIVE" : "STATUS";
}

String boardActivityText() {
  return boardNotificationActive() ? boardNotificationText() : boardIdleStatusText();
}

uint16_t boardActivityAccentColor() {
#if defined(ARDUINO_DONGLES3)
  return boardNotificationActive() ? g_dongleNotificationColor : boardConnectionAccentColor();
#else
  return boardConnectionAccentColor();
#endif
}

String fitBoardTextToWidth(const String& text, int16_t width, uint8_t size) {
  String safeText = text.isEmpty() ? "-" : text;
  if (boardTextWidth(safeText, size) <= width) {
    return safeText;
  }

  while (safeText.length() > 1) {
    safeText.remove(safeText.length() - 1);
    String candidate = safeText + "...";
    if (boardTextWidth(candidate, size) <= width) {
      return candidate;
    }
  }

  return ".";
}

int16_t boardRowLabelWidth(const String& label) {
  int16_t width = static_cast<int16_t>(boardTextWidth(label, 1) + 18);
  if (width < 46) {
    width = 46;
  }
  if (width > 64) {
    width = 64;
  }
  return width;
}

int16_t boardRowViewportWidth(const String& label) {
  if (label.isEmpty()) {
    return 128;
  }

  const int16_t rowWidth = 148;
  const int16_t textStart = 6 + 10 + boardRowLabelWidth(label) + 8;
  return rowWidth - (textStart - 6) - 10;
}

bool boardLineNeedsAnimation(const String& label, const String& text) {
  return boardTextWidth(text, 1) > boardRowViewportWidth(label);
}

int16_t boardMarqueeOffset(int16_t textWidth, int16_t viewportWidth, int16_t gap, uint32_t phaseMs) {
  if (textWidth <= viewportWidth) {
    return 0;
  }

  const uint32_t pauseMs = 900UL;
  const uint32_t speedMsPerPixel = 35UL;
  const int16_t travel = textWidth - viewportWidth + gap;
  const uint32_t moveMs = static_cast<uint32_t>(travel) * speedMsPerPixel;
  const uint32_t cycleMs = pauseMs + moveMs + pauseMs;
  const uint32_t cyclePos = (millis() + phaseMs) % cycleMs;

  if (cyclePos < pauseMs) {
    return 0;
  }
  if (cyclePos < pauseMs + moveMs) {
    return static_cast<int16_t>((cyclePos - pauseMs) / speedMsPerPixel);
  }
  return travel;
}

void drawBoardBackdrop() {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  g_dongleDisplay.fillScreen(boardBackgroundColor());
  g_dongleDisplay.fillCircle(148, -8, 34, boardColor(11, 24, 34));
  g_dongleDisplay.fillCircle(8, 88, 40, boardColor(10, 21, 29));
#endif
}

void initBoardFeedback() {
#if defined(ARDUINO_DONGLES3)
  pinMode(STINGRAY_DONGLE_LCD_BCKL, OUTPUT);
  digitalWrite(STINGRAY_DONGLE_LCD_BCKL, HIGH);

  g_dongleDisplaySpi.begin(STINGRAY_DONGLE_LCD_CLK, -1, STINGRAY_DONGLE_LCD_MOSI, STINGRAY_DONGLE_LCD_CS);
  g_dongleDisplay.initR(INITR_MINI160x80_PLUGIN);
  g_dongleDisplay.setRotation(1);
  g_dongleDisplay.invertDisplay(true);
  g_dongleDisplay.fillScreen(boardBackgroundColor());
  g_dongleDisplay.setTextWrap(false);
  digitalWrite(STINGRAY_DONGLE_LCD_BCKL, LOW);
  g_dongleDisplayReady = true;

  FastLED.addLeds<APA102, STINGRAY_DONGLE_LED_DI, STINGRAY_DONGLE_LED_CI, BGR>(g_dongleLed, 1);
  FastLED.setBrightness(100);
  g_dongleLed[0] = CHSV(g_dongleLedHue, 0xFF, 100);
  FastLED.show();
  g_dongleLedReady = true;

  g_dongleLastEventTitle = "BOOT";
  g_dongleLastEventLine1 = String(BOARD_NAME);
  g_dongleLastEventLine2 = "Starting firmware";
  g_dongleNextScreenRefreshAt = 0;
#endif
}

void drawBoardHeader(const String& title, const String& stateLabel, uint16_t accentColor) {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  const int16_t x = 4;
  const int16_t y = 4;
  const int16_t w = 152;
  const int16_t h = 18;
  const uint8_t titleSize = boardTextWidth(title, 2) <= 84 ? 2 : 1;
  const String safeTitle = titleSize == 2 ? truncateDisplayText(title, 11) : truncateDisplayText(title, 18);
  int16_t pillWidth = static_cast<int16_t>(boardTextWidth(stateLabel, 1) + 16);
  if (pillWidth < 42) {
    pillWidth = 42;
  }

  g_dongleDisplay.fillRoundRect(x, y, w, h, 6, boardPanelElevatedColor());
  g_dongleDisplay.drawRoundRect(x, y, w, h, 6, boardBorderColor());
  g_dongleDisplay.fillRect(x + 8, y + h - 3, w - 16, 2, accentColor);

  g_dongleDisplay.setTextWrap(false);
  g_dongleDisplay.setTextSize(titleSize);
  g_dongleDisplay.setTextColor(boardTextColor(), boardPanelElevatedColor());
  g_dongleDisplay.setCursor(x + 8, titleSize == 2 ? y + 2 : y + 5);
  g_dongleDisplay.print(safeTitle);

  const int16_t pillX = x + w - pillWidth - 6;
  g_dongleDisplay.fillRoundRect(pillX, y + 4, pillWidth, 10, 4, accentColor);
  g_dongleDisplay.setTextSize(1);
  g_dongleDisplay.setTextColor(boardBackgroundColor(), accentColor);
  g_dongleDisplay.setCursor(pillX + 8, y + 5);
  g_dongleDisplay.print(truncateDisplayText(stateLabel, 10));
#else
  (void)title;
  (void)stateLabel;
  (void)accentColor;
#endif
}

void drawBoardMarqueeText(int16_t x, int16_t y, int16_t viewportWidth, const String& text, uint8_t size, uint16_t textColor, uint16_t backgroundColor, uint32_t phaseMs) {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  const String safeText = text.isEmpty() ? "-" : text;
  const int16_t textWidth = boardTextWidth(safeText, size);
  const int16_t textHeight = boardTextHeight(size);
  const int16_t gap = 20;
  const int16_t offset = boardMarqueeOffset(textWidth, viewportWidth, gap, phaseMs);
  const int16_t textY = y;

  g_dongleDisplay.fillRect(x, y - 1, viewportWidth, textHeight + 2, backgroundColor);
  g_dongleDisplay.setTextWrap(false);
  g_dongleDisplay.setTextSize(size);
  g_dongleDisplay.setTextColor(textColor, backgroundColor);

  if (textWidth <= viewportWidth) {
    g_dongleDisplay.setCursor(x, textY);
    g_dongleDisplay.print(safeText);
    return;
  }

  const int16_t firstX = x - offset;
  g_dongleDisplay.setCursor(firstX, textY);
  g_dongleDisplay.print(safeText);

  if (firstX + textWidth + gap < x + viewportWidth + gap) {
    g_dongleDisplay.setCursor(firstX + textWidth + gap, textY);
    g_dongleDisplay.print(safeText);
  }
#else
  (void)x;
  (void)y;
  (void)viewportWidth;
  (void)text;
  (void)size;
  (void)textColor;
  (void)backgroundColor;
  (void)phaseMs;
#endif
}

void drawBoardRow(int16_t y, const String& label, const String& text, uint16_t accentColor, uint32_t phaseMs, bool allowScroll) {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  const int16_t x = 6;
  const int16_t w = 148;
  const int16_t h = 14;
  const bool showLabel = !label.isEmpty();
  const int16_t labelWidth = showLabel ? boardRowLabelWidth(label) : 0;
  const int16_t textX = showLabel ? (x + 10 + labelWidth + 8) : (x + 14);
  const int16_t viewportWidth = boardRowViewportWidth(label);

  g_dongleDisplay.fillRoundRect(x, y, w, h, 5, boardPanelColor());
  g_dongleDisplay.drawRoundRect(x, y, w, h, 5, boardBorderColor());
  g_dongleDisplay.fillRoundRect(x + 5, y + 3, 3, h - 6, 2, accentColor);
  if (allowScroll) {
    drawBoardMarqueeText(textX, y + 3, viewportWidth, text, 1, boardTextColor(), boardPanelColor(), phaseMs);
  } else {
    const String fittedText = fitBoardTextToWidth(text, viewportWidth, 1);
    g_dongleDisplay.fillRect(textX, y + 2, viewportWidth, boardTextHeight(1) + 3, boardPanelColor());
    g_dongleDisplay.setTextWrap(false);
    g_dongleDisplay.setTextSize(1);
    g_dongleDisplay.setTextColor(boardTextColor(), boardPanelColor());
    g_dongleDisplay.setCursor(textX, y + 3);
    g_dongleDisplay.print(fittedText);
  }

  if (showLabel) {
    g_dongleDisplay.fillRoundRect(x + 6, y + 2, labelWidth, h - 4, 4, accentColor);
    g_dongleDisplay.setTextWrap(false);
    g_dongleDisplay.setTextSize(1);
    g_dongleDisplay.setTextColor(boardBackgroundColor(), accentColor);
    g_dongleDisplay.setCursor(x + 10, y + 3);
    g_dongleDisplay.print(truncateDisplayText(label, 9));
  }
#else
  (void)y;
  (void)label;
  (void)text;
  (void)accentColor;
  (void)phaseMs;
#endif
}

String boardStaticSignature() {
  return boardConnectionStateLabel() + "|" + String(boardConnectionAccentColor()) + "|" + boardIdleNetworkText() + "|" + boardIdleAddressText();
}

String boardActivitySignature() {
  return boardActivityLabel() + "|" + String(boardActivityAccentColor()) + "|" + boardActivityText();
}

bool boardNotificationActive() {
#if defined(ARDUINO_DONGLES3)
  return g_dongleNotificationExpiresAt > millis();
#else
  return false;
#endif
}

bool boardScreenNeedsAnimation() {
  return boardLineNeedsAnimation("", boardActivityText());
}

void renderBoardActivityRow() {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  drawBoardRow(60, "", boardActivityText(), boardActivityAccentColor(), 0, true);
#endif
}

void renderBoardIdleScreen() {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  drawBoardBackdrop();
  drawBoardHeader("STINGRAY", boardConnectionStateLabel(), boardConnectionAccentColor());
  drawBoardRow(24, "", boardIdleNetworkText(), boardRowLabelColor(), 0, false);
  drawBoardRow(42, "", boardIdleAddressText(), boardColor(44, 170, 190), 0, false);
  renderBoardActivityRow();
#endif
}

void renderBoardNotificationScreen() {
  renderBoardIdleScreen();
}

void refreshBoardFeedback() {
#if defined(ARDUINO_DONGLES3)
  if (!g_dongleDisplayReady) {
    return;
  }

  renderBoardIdleScreen();
  g_dongleLastStaticSignature = boardStaticSignature();
  g_dongleLastActivitySignature = boardActivitySignature();
  g_dongleNextScreenRefreshAt = millis() + (boardScreenNeedsAnimation() ? 90UL : 1200UL);
#endif
}

void showBoardNotification(const String& title, const String& line1, const String& line2, uint16_t color, uint32_t durationMs) {
#if defined(ARDUINO_DONGLES3)
  g_dongleLastEventTitle = title;
  g_dongleLastEventLine1 = line1;
  g_dongleLastEventLine2 = line2;
  g_dongleNotificationTitle = title;
  g_dongleNotificationLine1 = line1;
  g_dongleNotificationLine2 = line2;
  g_dongleNotificationColor = color;
  g_dongleNotificationExpiresAt = millis() + durationMs;
  g_dongleNextScreenRefreshAt = 0;
  refreshBoardFeedback();
#else
  (void)title;
  (void)line1;
  (void)line2;
  (void)color;
  (void)durationMs;
#endif
}

void showBoardStatus(const String& title, const String& line1, const String& line2 = "") {
#if defined(ARDUINO_DONGLES3)
  showBoardNotification(title, line1, line2, boardColor(52, 121, 196), 4000UL);
#else
  (void)title;
  (void)line1;
  (void)line2;
#endif
}

void notifyInventoryCreatedOnBoard(const ItemRecord& item) {
#if defined(ARDUINO_DONGLES3)
  const String line1 = item.id + " " + itemDisplayName(item);
  const String line2 = "Qty " + String(item.qty);
  showBoardNotification("ITEM ADDED", line1, line2, boardColor(45, 183, 123), 5000UL);
#else
  (void)item;
#endif
}

void notifyInventoryRemovedOnBoard(const ItemRecord& item) {
#if defined(ARDUINO_DONGLES3)
  showBoardNotification("ITEM REMOVED", item.id, itemDisplayName(item), boardColor(223, 84, 79), 5000UL);
#else
  (void)item;
#endif
}

void notifyInventoryAdjustedOnBoard(const ItemRecord& item, int32_t delta) {
#if defined(ARDUINO_DONGLES3)
  const String deltaLabel = delta > 0 ? "+" + String(delta) : String(delta);
  const String line1 = item.id + " Qty " + String(item.qty);
  const String line2 = "Change " + deltaLabel;
  showBoardNotification("INVENTORY", line1, line2, delta >= 0 ? boardColor(45, 183, 123) : boardColor(214, 162, 59), 5000UL);
#else
  (void)item;
  (void)delta;
#endif
}

void notifyInventorySetQtyOnBoard(const ItemRecord& item, int32_t previousQty) {
#if defined(ARDUINO_DONGLES3)
  const int32_t delta = item.qty - previousQty;
  const String deltaLabel = delta > 0 ? "+" + String(delta) : String(delta);
  const String line1 = item.id + " Qty " + String(item.qty);
  const String line2 = delta == 0 ? "No change" : "Delta " + deltaLabel;
  showBoardNotification("QTY SET", line1, line2, boardColor(46, 176, 198), 5000UL);
#else
  (void)item;
  (void)previousQty;
#endif
}

void updateBoardFeedback() {
#if defined(ARDUINO_DONGLES3)
  if (g_dongleLedReady && millis() >= g_dongleNextLedUpdateAt) {
    g_dongleNextLedUpdateAt = millis() + 50UL;
    g_dongleLed[0] = CHSV(g_dongleLedHue++, 0xFF, 100);
    FastLED.show();
  }

  if (g_dongleNotificationExpiresAt != 0 && g_dongleNotificationExpiresAt <= millis()) {
    g_dongleNotificationExpiresAt = 0;
    g_dongleNotificationTitle = "";
    g_dongleNotificationLine1 = "";
    g_dongleNotificationLine2 = "";
  }

  const String staticSignature = boardStaticSignature();
  const String activitySignature = boardActivitySignature();

  if (g_dongleLastStaticSignature != staticSignature) {
    refreshBoardFeedback();
    return;
  }

  if (g_dongleLastActivitySignature != activitySignature) {
    renderBoardActivityRow();
    g_dongleLastActivitySignature = activitySignature;
    g_dongleNextScreenRefreshAt = millis() + (boardScreenNeedsAnimation() ? 90UL : 1200UL);
    return;
  }

  if (boardScreenNeedsAnimation() && millis() >= g_dongleNextScreenRefreshAt) {
    renderBoardActivityRow();
    g_dongleNextScreenRefreshAt = millis() + 90UL;
  }
#endif
}

String deviceId() {
  const uint64_t efuse = ESP.getEfuseMac();
  char suffix[13];
  snprintf(suffix, sizeof(suffix), "%04X%08X", static_cast<uint16_t>(efuse >> 32), static_cast<uint32_t>(efuse));
  return String(BOARD_NAME) + "-" + String(suffix);
}

String macAddressString() {
  const uint64_t efuse = ESP.getEfuseMac();
  const uint8_t mac[6] = {
    static_cast<uint8_t>((efuse >> 40) & 0xFF),
    static_cast<uint8_t>((efuse >> 32) & 0xFF),
    static_cast<uint8_t>((efuse >> 24) & 0xFF),
    static_cast<uint8_t>((efuse >> 16) & 0xFF),
    static_cast<uint8_t>((efuse >> 8) & 0xFF),
    static_cast<uint8_t>(efuse & 0xFF),
  };

  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

String resetReasonString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_EXT:
      return "external_pin";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
      return "task_watchdog";
    case ESP_RST_WDT:
      return "other_watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep_sleep_wake";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

bool timeIsSynchronized() {
  return time(nullptr) >= 1700000000;
}

String currentTimeSource() {
  return timeIsSynchronized() ? "ntp" : "uptime";
}

CloudBackupConfig defaultCloudBackupConfig() {
  CloudBackupConfig config;
  config.provider = "google_drive";
  config.loginEmail = "";
  config.folderName = "Stingray Inventory Backups";
  config.folderHint = "";
  config.mode = "select_or_create";
  config.backupMode = "sd_only";
  config.assetMode = "sd_only";
  config.brandName = "Stingray Inventory";
  config.brandLogoRef = "";
  config.clientId = "";
  config.clientSecret = "";
  config.updatedAt = "";
  return config;
}

WifiConfig defaultWifiConfig() {
  WifiConfig config;
  config.ssid = "";
  config.password = "";
  config.updatedAt = "";
  return config;
}

GoogleDriveState defaultGoogleDriveState() {
  GoogleDriveState state;
  state.refreshToken = "";
  state.folderId = "";
  state.lastSyncAt = "";
  state.lastSyncedManifestHash = "";
  state.lastSyncedSnapshotAt = "";
  state.localSnapshotAt = "";
  state.authStatus = "disconnected";
  state.syncStatus = "idle";
  state.lastError = "";
  state.accessToken = "";
  state.tokenType = "Bearer";
  state.scope = GOOGLE_DRIVE_SCOPE;
  state.deviceCode = "";
  state.userCode = "";
  state.verificationUrl = "";
  state.devicePollIntervalSeconds = 5;
  state.accessTokenExpiresAt = 0;
  return state;
}

String lowerCopy(String value) {
  value.toLowerCase();
  return value;
}

String normalizeLookupValue(String value) {
  value = trimCopy(value);
  value.toLowerCase();
  return value;
}

bool matchesCategoryFilter(const ItemRecord& item, const String& rawFilter) {
  String filter = trimCopy(rawFilter);
  filter.toLowerCase();

  if (filter.isEmpty() || filter == "all") {
    return true;
  }

  return normalizeCategory(item.category) == normalizeCategory(filter);
}

bool matchesSearchFilter(const ItemRecord& item, const String& rawSearch) {
  String search = lowerCopy(trimCopy(rawSearch));
  if (search.isEmpty()) {
    return true;
  }

  String haystack = item.id;
  haystack += " ";
  haystack += item.category;
  haystack += " ";
  haystack += item.partName;
  haystack += " ";
  haystack += item.qrCode;
  haystack += " ";
  haystack += item.color;
  haystack += " ";
  haystack += item.material;
  haystack += " ";
  haystack += item.imageRef;
  haystack += " ";
  haystack += item.bomProduct;
  haystack += " ";
  haystack += String(item.bomQty);

  haystack.toLowerCase();
  return haystack.indexOf(search) >= 0;
}

std::vector<String> splitPipeLine(const String& line) {
  std::vector<String> fields;
  int start = 0;

  while (true) {
    const int separator = line.indexOf('|', start);
    if (separator < 0) {
      fields.push_back(line.substring(start));
      break;
    }

    fields.push_back(line.substring(start, separator));
    start = separator + 1;
  }

  return fields;
}

String inventoryHeaderLine() {
  return "part_number|category|part_name|qr_code|color|material|qty|image_ref|bom_product|bom_qty|updated_at";
}

String cloudConfigHeaderLine() {
  return "provider|login_email|folder_name|folder_hint|mode|backup_mode|asset_mode|brand_name|brand_logo_ref|client_id|client_secret|updated_at";
}

String googleDriveStateHeaderLine() {
  return "refresh_token|folder_id|last_sync_at|last_synced_manifest_hash|last_synced_snapshot_at|local_snapshot_at|auth_status|sync_status|last_error";
}

bool wifiConfigUsable(const WifiConfig& config) {
  const String ssid = trimCopy(config.ssid);
  if (ssid.isEmpty() || ssid == "YOUR_WIFI_SSID") {
    return false;
  }

  const String password = config.password;
  if (password == "YOUR_WIFI_PASSWORD") {
    return false;
  }

  return true;
}

WifiConfig compileTimeWifiConfig() {
  WifiConfig config;
  config.ssid = String(WIFI_SSID);
  config.password = String(WIFI_PASSWORD);
  config.updatedAt = "";
  return config;
}

WifiConfig effectiveWifiConfig() {
  if (wifiConfigUsable(g_wifiConfig)) {
    return g_wifiConfig;
  }

  const WifiConfig fallback = compileTimeWifiConfig();
  if (wifiConfigUsable(fallback)) {
    return fallback;
  }

  return defaultWifiConfig();
}

bool wifiCredentialsConfigured() {
  return wifiConfigUsable(effectiveWifiConfig());
}

String wifiConfigSource() {
  if (wifiConfigUsable(g_wifiConfig)) {
    return "saved";
  }

  if (wifiConfigUsable(compileTimeWifiConfig())) {
    return "compile_time";
  }

  return "none";
}

bool wifiApActive() {
  const wifi_mode_t mode = WiFi.getMode();
  return mode == WIFI_AP || mode == WIFI_AP_STA;
}

String wifiModeLabel() {
  switch (WiFi.getMode()) {
    case WIFI_OFF:
      return "off";
    case WIFI_STA:
      return "station";
    case WIFI_AP:
      return "access_point";
    case WIFI_AP_STA:
      return "access_point_station";
    default:
      return "unknown";
  }
}

String wifiStatusLabel(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_found";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "unknown";
  }
}

String wifiEncryptionLabel(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return "open";
    case WIFI_AUTH_WEP:
      return "wep";
    case WIFI_AUTH_WPA_PSK:
      return "wpa";
    case WIFI_AUTH_WPA2_PSK:
      return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "wpa_wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "wpa2_enterprise";
    case WIFI_AUTH_WPA3_PSK:
      return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "wpa2_wpa3";
    default:
      return "secured";
  }
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_FALLBACK_AP_NAME);
  g_pendingApShutdown = false;
  g_pendingApShutdownAt = 0;
  g_baseUrl = "http://" + WiFi.softAPIP().toString();
  g_timeSource = "uptime";
  Serial.print("AP URL: ");
  Serial.println(g_baseUrl);
}

void scheduleAccessPointShutdown(uint32_t delayMs) {
  if (!wifiApActive() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  g_pendingApShutdown = true;
  g_pendingApShutdownAt = millis() + delayMs;
}

void processPendingAccessPointShutdown() {
  if (!g_pendingApShutdown || millis() < g_pendingApShutdownAt) {
    return;
  }

  g_pendingApShutdown = false;
  g_pendingApShutdownAt = 0;

  if (!wifiApActive() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  showBoardStatus("WIFI READY", currentNetworkDisplayLine(), "AP off");
}

fs::FS& storageFs() {
#if defined(STINGRAY_USE_SD_MMC)
  return SD_MMC;
#else
  return SD;
#endif
}

bool initStorage() {
#if defined(STINGRAY_USE_SD_MMC)
  Serial.printf("Initializing SD_MMC storage for %s\n", BOARD_NAME);
  Serial.printf("SD_MMC pins CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d\n",
    STINGRAY_SD_MMC_CLK,
    STINGRAY_SD_MMC_CMD,
    STINGRAY_SD_MMC_D0,
    STINGRAY_SD_MMC_D1,
    STINGRAY_SD_MMC_D2,
    STINGRAY_SD_MMC_D3);

  if (!SD_MMC.setPins(
    STINGRAY_SD_MMC_CLK,
    STINGRAY_SD_MMC_CMD,
    STINGRAY_SD_MMC_D0,
    STINGRAY_SD_MMC_D1,
    STINGRAY_SD_MMC_D2,
    STINGRAY_SD_MMC_D3
  )) {
    Serial.println("SD_MMC pin assignment failed.");
    return false;
  }

  return SD_MMC.begin("/sdcard", false, false);
#else
  Serial.printf("Initializing SPI SD storage for %s\n", BOARD_NAME);
  Serial.printf("SPI pins SCK=%d MISO=%d MOSI=%d CS=%d\n",
    STINGRAY_SD_SPI_SCK,
    STINGRAY_SD_SPI_MISO,
    STINGRAY_SD_SPI_MOSI,
    STINGRAY_SD_SPI_CS);

  SPI.begin(
    STINGRAY_SD_SPI_SCK,
    STINGRAY_SD_SPI_MISO,
    STINGRAY_SD_SPI_MOSI,
    STINGRAY_SD_SPI_CS
  );
  return SD.begin(STINGRAY_SD_SPI_CS, SPI);
#endif
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Stingray Inventory</title>
  <style>
    :root {
      --bg: #eef2f4;
      --panel: #ffffff;
      --line: #d5dde2;
      --ink: #1b2f3f;
      --muted: #637689;
      --accent: #0f7a72;
      --accent-dark: #0a5f59;
      --danger: #bb3030;
      --danger-bg: #fff1f1;
      --zero-text: #8f1f1f;
      --shadow: 0 14px 34px rgba(17, 35, 54, 0.1);
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: "Segoe UI", Tahoma, sans-serif;
      background:
        radial-gradient(circle at top left, #f9fbfc 0, #eef2f4 34%, #e4eaee 100%);
      color: var(--ink);
    }

    header {
      padding: 1.1rem 1rem 0.95rem;
      background: linear-gradient(120deg, #fbfdfd, #edf4f6);
      border-bottom: 1px solid var(--line);
    }

    .header-shell {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 1rem;
      flex-wrap: wrap;
    }

    .brand-shell {
      display: flex;
      align-items: center;
      gap: 0.9rem;
    }

    .brand-logo {
      width: 68px;
      height: 68px;
      object-fit: contain;
      border-radius: 16px;
      border: 1px solid var(--line);
      background: #ffffff;
      padding: 0.45rem;
      box-shadow: var(--shadow);
    }

    .brand-copy {
      min-width: 0;
    }

    #host-info {
      word-break: break-word;
    }

    .header-actions {
      display: flex;
      flex-wrap: wrap;
      gap: 0.6rem;
    }

    header h1 { margin: 0; font-size: 1.45rem; }
    header p {
      margin: 0.3rem 0 0;
      color: var(--muted);
      overflow-wrap: anywhere;
    }

    main {
      max-width: 1450px;
      margin: 0 auto;
      padding: 1rem;
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 1rem;
    }

    section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      padding: 1rem;
      box-shadow: var(--shadow);
      min-width: 0;
    }

    section.wide { grid-column: 1 / -1; }
    h2 {
      margin-top: 0;
      margin-bottom: 0.35rem;
      font-size: 1.08rem;
    }

    label {
      display: block;
      margin-top: 0.5rem;
      margin-bottom: 0.25rem;
      color: var(--muted);
      font-size: 0.9rem;
    }

    label span {
      display: block;
      overflow-wrap: anywhere;
    }

    input, select, button {
      width: 100%;
      border-radius: 12px;
      font: inherit;
    }

    input, select {
      border: 1px solid var(--line);
      padding: 0.68rem 0.75rem;
      background: #fff;
      color: var(--ink);
    }

    button {
      border: 1px solid var(--accent);
      background: var(--accent);
      color: #fff;
      padding: 0.68rem 0.85rem;
      cursor: pointer;
      font-weight: 700;
    }

    button.secondary {
      border-color: var(--line);
      background: #f5f8fb;
      color: var(--ink);
    }

    button.danger {
      border-color: var(--danger);
      background: var(--danger);
    }

    button:hover {
      filter: brightness(1.02);
    }

    .nav-link {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-width: 122px;
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 0.68rem 0.85rem;
      background: #f5f8fb;
      color: var(--ink);
      text-decoration: none;
      font-weight: 700;
      white-space: nowrap;
    }

    .nav-link:hover {
      filter: brightness(1.02);
    }

    .stack {
      display: grid;
      gap: 0.75rem;
    }

    .form-grid,
    .controls-grid {
      display: grid;
      gap: 0.8rem;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    }

    .span-2 {
      grid-column: span 2;
    }

    .tab-strip,
    .export-strip {
      display: flex;
      flex-wrap: wrap;
      gap: 0.6rem;
    }

    .tab-strip button,
    .export-strip button {
      width: auto;
      min-width: 0;
      flex: 1 1 130px;
    }

    .tab-strip button.orders-tab {
      flex: 2 1 260px;
    }

    .tab-strip button.active {
      background: linear-gradient(180deg, var(--accent), var(--accent-dark));
      border-color: var(--accent-dark);
      color: #fff;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.92rem;
    }

    th, td {
      border-bottom: 1px solid var(--line);
      text-align: left;
      padding: 0.5rem;
      vertical-align: top;
      overflow-wrap: anywhere;
      word-break: break-word;
    }

    td code {
      font-size: 0.8rem;
      word-break: break-all;
    }

    th {
      background: #f7fafb;
      position: sticky;
      top: 0;
      z-index: 1;
    }

    .table-wrap {
      overflow-x: auto;
      border: 1px solid var(--line);
      border-radius: 14px;
      background: #fff;
    }

    .quick-action h2 {
      margin: 0 0 0.85rem;
    }

    .action-grid {
      display: grid;
      gap: 0.8rem;
      grid-template-columns: minmax(0, 1fr);
      align-items: start;
    }

    .action-remove {
      display: grid;
      gap: 0.45rem;
      min-width: 0;
    }

    .action-remove-row {
      display: grid;
      gap: 0.65rem;
      grid-template-columns: minmax(0, 1fr);
      align-items: stretch;
    }

    .action-remove-row button {
      width: 100%;
    }

    .settings-inline-row {
      display: grid;
      gap: 0.65rem;
      grid-template-columns: minmax(0, 1fr) auto;
      align-items: center;
    }

    .settings-inline-row button {
      width: auto;
      min-width: 150px;
    }

    .quick-action button {
      min-height: 54px;
      font-size: 1rem;
    }

    .draft-row td {
      background: #f8fbfd;
    }

    .draft-row input,
    .draft-row select {
      width: 100%;
      min-width: 0;
      padding: 0.55rem 0.6rem;
      border: 1px solid var(--line);
      border-radius: 10px;
      background: #fff;
      color: var(--ink);
      font: inherit;
    }

    .cell-stack {
      display: grid;
      gap: 0.38rem;
      min-width: 120px;
    }

    .cell-stack input[hidden] {
      display: none;
    }

    .cell-note {
      font-size: 0.74rem;
      color: var(--muted);
      line-height: 1.35;
    }

    .inline-actions {
      display: grid;
      gap: 0.42rem;
      min-width: 100px;
    }

    .log-grid {
      display: grid;
      gap: 0.8rem;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    }

    .orders-toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 0.6rem;
      margin: 0 0 0.85rem;
    }

    .orders-toolbar button {
      width: auto;
      min-width: 160px;
    }

    .orders-item-select {
      min-width: 220px;
    }

    .orders-needed-input {
      max-width: 110px;
    }

    .orders-available {
      color: #0f7a72;
      font-weight: 700;
    }

    .orders-shortage {
      color: #9f2121;
      font-weight: 700;
    }

    .orders-footer {
      display: flex;
      justify-content: flex-end;
      margin-top: 0.85rem;
    }

    .orders-footer button {
      width: auto;
      min-width: 190px;
    }

    .log-panel {
      border: 1px solid var(--line);
      border-radius: 14px;
      background: #fff;
      padding: 0.8rem;
    }

    .log-panel h3 {
      margin: 0 0 0.5rem;
      font-size: 0.95rem;
    }

    .log-output {
      min-height: 220px;
      max-height: 320px;
      overflow: auto;
      margin: 0;
      font-family: Consolas, "Courier New", monospace;
      font-size: 0.8rem;
      line-height: 1.45;
      white-space: pre-wrap;
      word-break: break-word;
    }

    tr.stock-zero td {
      color: var(--zero-text);
      background: var(--danger-bg);
      font-weight: 700;
    }

    .qty-pill {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-width: 44px;
      padding: 0.28rem 0.55rem;
      border-radius: 999px;
      background: #edf7f6;
      color: var(--accent-dark);
      font-weight: 800;
    }

    tr.stock-zero .qty-pill {
      background: #f9dada;
      color: var(--zero-text);
    }

    .meta-chip {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 0.26rem 0.55rem;
      border-radius: 999px;
      background: #edf3f8;
      color: var(--ink);
      font-weight: 700;
      text-transform: capitalize;
    }

    button.asset-trigger,
    .qr-modal-actions button,
    .qr-modal-head button,
    .image-modal-head button {
      width: auto;
    }

    button.asset-trigger {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 0.18rem;
      border: 1px solid transparent;
      border-radius: 10px;
      background: transparent;
    }

    button.asset-trigger:hover {
      background: #f5f8fb;
      border-color: var(--line);
      filter: none;
    }

    .asset-preview {
      display: block;
      width: 27px;
      height: 27px;
      padding: 0.08rem;
      border: 1px solid var(--line);
      border-radius: 6px;
      background: #fff;
      object-fit: cover;
    }

    .qr-preview {
      image-rendering: pixelated;
      object-fit: contain;
    }

    .asset-cell {
      text-align: center;
      width: 78px;
    }

    .qr-modal[hidden] {
      display: none;
    }

    .qr-modal {
      position: fixed;
      inset: 0;
      z-index: 20;
      display: grid;
      place-items: center;
      padding: 1rem;
    }

    .qr-modal-backdrop {
      position: absolute;
      inset: 0;
      background: rgba(18, 30, 42, 0.62);
    }

    .qr-modal-card {
      position: relative;
      width: min(420px, 100%);
      border: 1px solid var(--line);
      border-radius: 18px;
      background: #fff;
      box-shadow: 0 20px 42px rgba(17, 35, 54, 0.28);
      padding: 1rem;
    }

    .qr-modal-head,
    .qr-modal-actions {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 0.75rem;
      flex-wrap: wrap;
    }

    .qr-modal-head h3 {
      margin: 0;
      font-size: 1.1rem;
    }

    .qr-modal-body {
      display: grid;
      gap: 0.75rem;
      margin-top: 0.9rem;
    }

    .qr-modal-image {
      display: block;
      width: min(240px, 70vw);
      height: auto;
      margin: 0 auto;
      padding: 0.9rem;
      border: 1px solid var(--line);
      border-radius: 16px;
      background: #fff;
      image-rendering: pixelated;
    }

    .image-modal-image {
      display: block;
      width: min(320px, 80vw);
      max-height: min(60vh, 420px);
      height: auto;
      margin: 0 auto;
      padding: 0.5rem;
      border: 1px solid var(--line);
      border-radius: 16px;
      background: #fff;
      object-fit: contain;
    }

    .qr-modal-link {
      display: block;
      padding: 0.75rem 0.85rem;
      border: 1px solid var(--line);
      border-radius: 12px;
      background: #f7fafb;
      color: var(--ink);
      text-decoration: none;
      font-size: 0.84rem;
      word-break: break-all;
    }

    body.qr-modal-open {
      overflow: hidden;
    }

    .small {
      font-size: 0.85rem;
      color: var(--muted);
    }

    .caption {
      margin: 0 0 0.85rem;
      color: var(--muted);
      font-size: 0.92rem;
    }

    .cloud-actions {
      display: flex;
      flex-wrap: wrap;
      gap: 0.65rem;
      margin-top: 0.9rem;
      align-items: center;
    }

    .cloud-actions button {
      width: auto;
      max-width: 100%;
    }

    .cloud-status {
      margin: 0.85rem 0 0;
      padding: 0.8rem;
      border: 1px solid var(--line);
      border-radius: 14px;
      background: #f7fafb;
      color: var(--ink);
      font-family: Consolas, "Courier New", monospace;
      font-size: 0.82rem;
      line-height: 1.45;
      white-space: pre-wrap;
      word-break: break-word;
      overflow-wrap: anywhere;
    }

    .status {
      position: fixed;
      right: 1rem;
      bottom: 1rem;
      padding: 0.7rem 0.85rem;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #ffffff;
      max-width: min(90vw, 580px);
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.16);
      overflow-wrap: anywhere;
      word-break: break-word;
    }

    .status.error { border-color: #efb6b6; color: #9f2121; background: #fff2f2; }
    .status.ok { border-color: #b7e2d9; color: #0e6a63; background: #edfdf8; }

    .cloud-online[hidden] {
      display: none !important;
    }

    @media (max-width: 980px) {
      .action-grid,
      .action-remove-row {
        grid-template-columns: 1fr;
      }
    }

    @media (max-width: 700px) {
      .span-2 {
        grid-column: span 1;
      }

      .action-grid,
      .action-remove-row,
      .settings-inline-row {
        grid-template-columns: 1fr;
      }

      .tab-strip button,
      .export-strip button,
      .orders-toolbar button,
      .orders-footer button {
        width: 100%;
      }

      .status {
        right: 0.6rem;
        left: 0.6rem;
        max-width: none;
      }

      .qr-modal-card {
        padding: 0.9rem;
      }
    }
  </style>
</head>
<body>
  <header>
    <div class="header-shell">
      <div class="brand-shell">
        <img id="brand-logo" class="brand-logo" alt="Brand logo" hidden>
        <div class="brand-copy">
          <h1 id="brand-title">Stingray Inventory</h1>
          <p class="small" id="host-info">Loading host info...</p>
        </div>
      </div>
      <div class="header-actions">
        <a id="inventory-nav-link" class="nav-link" href="/" hidden>Inventory</a>
        <a id="orders-nav-link" class="nav-link" href="/orders">Orders</a>
        <a id="settings-nav-link" class="nav-link" href="/settings">Settings</a>
      </div>
    </div>
  </header>

  <main>
    <section id="inventory-actions-section" class="quick-action">
      <h2>Inventory Actions</h2>
      <div class="action-grid">
        <button id="add-row-btn" type="button">Add Item</button>
        <form id="remove-form" class="action-remove">
          <label for="remove-id">Remove By Part Number</label>
          <div class="action-remove-row">
            <input id="remove-id" type="text" required>
            <button type="submit" class="danger">Remove Item</button>
          </div>
          <p class="small">Removing an item deletes it from current inventory and logs the action.</p>
        </form>
      </div>
    </section>

    <section id="inventory-tools-section">
      <h2>Views And Tools</h2>
      <div class="stack">
        <div>
          <label for="search-input">Search</label>
          <input id="search-input" type="search" placeholder="Search by part name, category, part number, QR code, color, material, or image ref">
        </div>

        <div>
          <div class="small">Inventory tabs</div>
          <div class="tab-strip">
            <button type="button" class="secondary active" data-category="all">All Inventory</button>
            <button type="button" class="secondary" data-category="part">Parts</button>
            <button type="button" class="secondary" data-category="product">Products</button>
            <button type="button" class="secondary" data-category="kit">Kits</button>
            <button id="orders-tab-btn" type="button" class="secondary orders-tab">Orders</button>
          </div>
        </div>

        <div>
          <div class="small">CSV exports</div>
          <div class="export-strip">
            <button id="refresh-btn" type="button">Refresh Inventory</button>
            <button class="secondary" type="button" data-export="all">Export All</button>
            <button class="secondary" type="button" data-export="part">Export Parts</button>
            <button class="secondary" type="button" data-export="product">Export Products</button>
            <button class="secondary" type="button" data-export="kit">Export Kits</button>
          </div>
        </div>
      </div>
    </section>

    <section id="orders-section" class="wide" hidden>
      <h2>Large Order Planner</h2>
      <p class="caption">Large orders only. Create an order, then open it to choose parts/products/kits and required quantities.</p>
      <div class="orders-toolbar">
        <button id="orders-add-btn" type="button">Add Order</button>
        <button id="orders-refresh-btn" type="button" class="secondary">Refresh Stock</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Order Number</th>
              <th>In Stock</th>
              <th>Out Of Stock</th>
              <th>Items Short</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody id="orders-list-body"></tbody>
        </table>
      </div>
      <pre id="orders-summary" class="cloud-status">Order planner ready.</pre>
    </section>

    <section id="order-detail-section" class="wide" hidden>
      <h2 id="order-detail-title">Order</h2>
      <p class="caption">Select parts, products, and kits for this order and set required quantities.</p>
      <div class="orders-toolbar">
        <button id="order-back-btn" type="button" class="secondary">Back To Orders</button>
        <button id="order-add-item-btn" type="button">Add Item</button>
        <button id="order-refresh-btn" type="button" class="secondary">Refresh Stock</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Part / Product / Kit</th>
              <th>In Stock</th>
              <th>Needed</th>
              <th>Out Of Stock</th>
              <th>Items Short</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody id="order-detail-body"></tbody>
        </table>
      </div>
      <pre id="order-detail-summary" class="cloud-status">Order detail ready.</pre>
      <div class="orders-footer">
        <button id="order-fulfill-btn" type="button" class="danger">Fulfill Order</button>
      </div>
    </section>

    <section id="order-fulfill-section" class="wide" hidden>
      <h2 id="order-fulfill-title">Fulfill Order</h2>
      <p class="caption">Review required items before removing stock. Only selected lines are included.</p>
      <div class="orders-toolbar">
        <button id="order-fulfill-back-btn" type="button" class="secondary">Back To Order</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Part Number</th>
              <th>Part / Product / Kit</th>
              <th>Category</th>
              <th>In Stock</th>
              <th>Needed</th>
              <th>Items Short</th>
            </tr>
          </thead>
          <tbody id="order-fulfill-body"></tbody>
        </table>
      </div>
      <pre id="order-fulfill-summary" class="cloud-status">Fulfillment review ready.</pre>
      <div class="orders-footer">
        <button id="order-fulfill-confirm-btn" type="button" class="danger">Fulfill Order</button>
      </div>
    </section>

    <section id="settings-section" class="wide">
      <h2>Wi-Fi Setup</h2>
      <p id="wifi-caption" class="caption">Scan nearby networks, save Wi-Fi credentials on the device, and connect without reflashing firmware.</p>
      <div class="form-grid">
        <label class="span-2">
          <span>Nearby Networks</span>
          <div class="settings-inline-row">
            <select id="wifi-network-select">
              <option value="">Choose a scanned network</option>
              <option value="__manual__">Enter network manually / hidden SSID</option>
            </select>
            <button id="wifi-scan-btn" type="button" class="secondary">Scan Networks</button>
          </div>
        </label>

        <label class="span-2">
          <span>Network Name (SSID)</span>
          <input id="wifi-ssid" type="text" placeholder="Choose a network or type one manually">
        </label>

        <label class="span-2">
          <span>Password</span>
          <input id="wifi-password" type="password" placeholder="Leave blank for open networks">
        </label>
      </div>
      <div class="cloud-actions">
        <button id="wifi-save-btn" type="button">Save And Connect Wi-Fi</button>
        <button id="wifi-forget-btn" type="button" class="secondary">Forget Saved Wi-Fi</button>
      </div>
      <pre id="wifi-status" class="cloud-status">Loading Wi-Fi status...</pre>

      <h2>Storage And Branding</h2>
      <p id="cloud-caption" class="caption">Standalone mode keeps inventory, UI assets, logs, and branding on the SD card.</p>
      <div class="form-grid">
        <label>
          <span>Backup Mode</span>
          <select id="backup-mode">
            <option value="sd_only">Standalone SD only</option>
            <option value="hybrid_sd_google">Hybrid SD + Google backup</option>
            <option value="google_primary_sd_fallback">Google primary with SD fallback</option>
          </select>
        </label>

        <label>
          <span>Asset Mode</span>
          <select id="asset-mode">
            <option value="sd_only">Assets on SD only</option>
            <option value="sd_primary_google_backup">Assets on SD with Google backup</option>
            <option value="google_primary_sd_fallback">Assets on Google with SD fallback</option>
          </select>
        </label>

        <label>
          <span>Brand Name</span>
          <input id="brand-name" type="text" placeholder="Stingray Inventory">
        </label>

        <label class="cloud-online" hidden>
          <span>Provider</span>
          <select id="cloud-provider">
            <option value="google_drive">Google Drive / Docs backup</option>
          </select>
        </label>

        <label class="cloud-online" hidden>
          <span>Google Account</span>
          <input id="cloud-login-email" type="text" placeholder="name@gmail.com">
        </label>

        <label class="cloud-online" hidden>
          <span>Folder Mode</span>
          <select id="cloud-folder-mode">
            <option value="select_or_create">Pick or create later</option>
            <option value="create">Create new folder</option>
            <option value="use_existing">Use existing folder</option>
          </select>
        </label>

        <label class="span-2 cloud-online" hidden>
          <span>Folder Name</span>
          <input id="cloud-folder-name" type="text" placeholder="Stingray Inventory Backups">
        </label>

        <label class="span-2 cloud-online" hidden>
          <span>Folder Hint / Link / ID</span>
          <input id="cloud-folder-hint" type="text" placeholder="Optional Google Drive folder link or folder ID">
        </label>

        <label class="span-2 cloud-online" hidden>
          <span>Google OAuth Client ID</span>
          <input id="google-client-id" type="text" placeholder="Google Cloud OAuth client ID">
        </label>

        <label class="span-2 cloud-online" hidden>
          <span>Google OAuth Client Secret</span>
          <input id="google-client-secret" type="password" placeholder="Google Cloud OAuth client secret">
        </label>

        <label class="span-2">
          <span>Brand Logo</span>
          <input id="brand-logo-file" type="file" accept="image/*">
        </label>
      </div>
      <div class="cloud-actions">
        <button id="save-cloud-btn" type="button">Save Storage And Branding Settings</button>
      </div>
      <div class="cloud-actions cloud-online" hidden>
        <button id="google-auth-start-btn" type="button" class="secondary">Start Google Login</button>
        <button id="google-auth-poll-btn" type="button" class="secondary">Check Google Login</button>
        <button id="google-sync-btn" type="button" class="secondary">Sync Now</button>
        <button id="google-restore-btn" type="button" class="secondary">Restore From Google</button>
        <button id="google-disconnect-btn" type="button" class="secondary">Disconnect Google</button>
      </div>
      <p id="brand-logo-note" class="small">No brand logo selected. Upload stores the file on the SD card.</p>
      <pre id="google-status" class="cloud-status cloud-online" hidden>Loading Google Drive status...</pre>
      <h2>Activity Log</h2>
      <div class="export-strip">
        <button id="refresh-logs-btn" type="button">Refresh Activity Log</button>
      </div>
      <pre id="device-log-output" class="log-output">Loading activity log...</pre>
    </section>

    <section id="inventory-table-section" class="wide">
      <h2 id="inventory-title">All Inventory</h2>
      <p class="caption" id="inventory-caption">Loading inventory...</p>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Part Number</th>
              <th>Category</th>
              <th>Part Name</th>
              <th>Color</th>
              <th>Material</th>
              <th>Qty</th>
              <th>Image</th>
              <th>QR Code</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody id="items-body"></tbody>
        </table>
      </div>
    </section>
  </main>

  <div id="qr-modal" class="qr-modal" hidden>
    <div class="qr-modal-backdrop" data-close-qr-modal></div>
    <div class="qr-modal-card" role="dialog" aria-modal="true" aria-labelledby="qr-modal-title">
      <div class="qr-modal-head">
        <h3 id="qr-modal-title">QR Code Preview</h3>
        <button id="qr-modal-close" type="button" class="secondary">Close</button>
      </div>
      <div class="qr-modal-body">
        <img id="qr-modal-image" class="qr-modal-image" alt="QR code preview">
        <div class="small">Encoded link</div>
        <a id="qr-modal-link" class="qr-modal-link" href="#" target="_blank" rel="noopener"></a>
      </div>
      <div class="qr-modal-actions">
        <span class="small">Use print for a full-size QR label.</span>
        <button id="qr-modal-print" type="button">Print QR Code</button>
      </div>
    </div>
  </div>

  <div id="image-modal" class="qr-modal" hidden>
    <div class="qr-modal-backdrop" data-close-image-modal></div>
    <div class="qr-modal-card" role="dialog" aria-modal="true" aria-labelledby="image-modal-title">
      <div class="qr-modal-head image-modal-head">
        <h3 id="image-modal-title">Image Preview</h3>
        <button id="image-modal-close" type="button" class="secondary">Close</button>
      </div>
      <div class="qr-modal-body">
        <img id="image-modal-image" class="image-modal-image" alt="Item image preview">
        <div class="small">Stored image reference</div>
        <div id="image-modal-ref" class="qr-modal-link"></div>
      </div>
    </div>
  </div>

  <div id="status" class="status">Ready.</div>

  <script>
    const brandTitle = document.getElementById('brand-title');
    const brandLogo = document.getElementById('brand-logo');
    const hostInfo = document.getElementById('host-info');
    const inventoryNavLink = document.getElementById('inventory-nav-link');
    const ordersNavLink = document.getElementById('orders-nav-link');
    const settingsNavLink = document.getElementById('settings-nav-link');
    const inventoryActionsSection = document.getElementById('inventory-actions-section');
    const inventoryToolsSection = document.getElementById('inventory-tools-section');
    const ordersSection = document.getElementById('orders-section');
    const orderDetailSection = document.getElementById('order-detail-section');
    const orderFulfillSection = document.getElementById('order-fulfill-section');
    const settingsSection = document.getElementById('settings-section');
    const inventoryTableSection = document.getElementById('inventory-table-section');
    const itemsBody = document.getElementById('items-body');
    const ordersListBody = document.getElementById('orders-list-body');
    const ordersSummary = document.getElementById('orders-summary');
    const orderDetailBody = document.getElementById('order-detail-body');
    const orderDetailSummary = document.getElementById('order-detail-summary');
    const orderDetailTitle = document.getElementById('order-detail-title');
    const orderFulfillBody = document.getElementById('order-fulfill-body');
    const orderFulfillSummary = document.getElementById('order-fulfill-summary');
    const orderFulfillTitle = document.getElementById('order-fulfill-title');
    const statusBox = document.getElementById('status');
    const inventoryTitle = document.getElementById('inventory-title');
    const inventoryCaption = document.getElementById('inventory-caption');
    const addRowBtn = document.getElementById('add-row-btn');
    const ordersTabBtn = document.getElementById('orders-tab-btn');
    const ordersAddBtn = document.getElementById('orders-add-btn');
    const ordersRefreshBtn = document.getElementById('orders-refresh-btn');
    const orderBackBtn = document.getElementById('order-back-btn');
    const orderAddItemBtn = document.getElementById('order-add-item-btn');
    const orderRefreshBtn = document.getElementById('order-refresh-btn');
    const orderFulfillBtn = document.getElementById('order-fulfill-btn');
    const orderFulfillBackBtn = document.getElementById('order-fulfill-back-btn');
    const orderFulfillConfirmBtn = document.getElementById('order-fulfill-confirm-btn');
    const wifiCaptionEl = document.getElementById('wifi-caption');
    const wifiNetworkSelectEl = document.getElementById('wifi-network-select');
    const wifiSsidEl = document.getElementById('wifi-ssid');
    const wifiPasswordEl = document.getElementById('wifi-password');
    const wifiScanBtn = document.getElementById('wifi-scan-btn');
    const wifiSaveBtn = document.getElementById('wifi-save-btn');
    const wifiForgetBtn = document.getElementById('wifi-forget-btn');
    const wifiStatusOutput = document.getElementById('wifi-status');
    const cloudCaption = document.getElementById('cloud-caption');
    const backupModeEl = document.getElementById('backup-mode');
    const assetModeEl = document.getElementById('asset-mode');
    const brandNameEl = document.getElementById('brand-name');
    const cloudProviderEl = document.getElementById('cloud-provider');
    const cloudLoginEmailEl = document.getElementById('cloud-login-email');
    const cloudFolderModeEl = document.getElementById('cloud-folder-mode');
    const cloudFolderNameEl = document.getElementById('cloud-folder-name');
    const cloudFolderHintEl = document.getElementById('cloud-folder-hint');
    const googleClientIdEl = document.getElementById('google-client-id');
    const googleClientSecretEl = document.getElementById('google-client-secret');
    const brandLogoFileEl = document.getElementById('brand-logo-file');
    const brandLogoNoteEl = document.getElementById('brand-logo-note');
    const saveCloudBtn = document.getElementById('save-cloud-btn');
    const googleAuthStartBtn = document.getElementById('google-auth-start-btn');
    const googleAuthPollBtn = document.getElementById('google-auth-poll-btn');
    const googleSyncBtn = document.getElementById('google-sync-btn');
    const googleRestoreBtn = document.getElementById('google-restore-btn');
    const googleDisconnectBtn = document.getElementById('google-disconnect-btn');
    const googleStatusOutput = document.getElementById('google-status');
    const cloudOnlineElements = document.querySelectorAll('.cloud-online');
    const refreshLogsBtn = document.getElementById('refresh-logs-btn');
    const deviceLogOutput = document.getElementById('device-log-output');
    const qrModal = document.getElementById('qr-modal');
    const qrModalImage = document.getElementById('qr-modal-image');
    const qrModalLink = document.getElementById('qr-modal-link');
    const qrModalClose = document.getElementById('qr-modal-close');
    const qrModalPrint = document.getElementById('qr-modal-print');
    const imageModal = document.getElementById('image-modal');
    const imageModalImage = document.getElementById('image-modal-image');
    const imageModalRef = document.getElementById('image-modal-ref');
    const imageModalClose = document.getElementById('image-modal-close');
    const CUSTOM_OPTION_VALUE = '__custom__';
    const RECENT_SELECTIONS_KEY = 'stingray_inventory_recent_values_v1';
    const MAX_ORDER_COUNT = 150;
    const MAX_ORDER_LINE_COUNT = 120;
    const isSettingsPage = window.location.pathname === '/settings';
    const isOrdersPage = window.location.pathname === '/orders';
    const isOrderDetailPage = window.location.pathname === '/orders/view';
    const isOrderFulfillPage = window.location.pathname === '/orders/fulfill';
    const isInventoryPage = !isSettingsPage && !isOrdersPage && !isOrderDetailPage && !isOrderFulfillPage;
    const orderQueryNumber = new URLSearchParams(window.location.search).get('order') || '';
    const CATEGORY_OPTIONS = [
      { value: 'product', label: 'Product' },
      { value: 'part', label: 'Part' },
      { value: 'kit', label: 'Kit' },
    ];

    const state = {
      baseUrl: window.location.origin,
      items: [],
      activeCategory: 'all',
      search: '',
      draftItem: null,
      draftImageFile: null,
      draftBrandLogoFile: null,
      cloudConfig: null,
      wifiConfig: null,
      wifiNetworks: [],
      orders: [],
      currentOrderNumber: '',
    };
    let activeQrPreview = null;
    let activeImagePreview = null;

    const tabLabels = {
      all: 'All Inventory',
      part: 'Parts',
      product: 'Products',
      kit: 'Kits',
    };

    function setStatus(message, kind = 'ok') {
      statusBox.textContent = message;
      statusBox.className = `status ${kind}`;
    }

    function cloudModeNeedsOnline(backupMode, assetMode) {
      const safeBackupMode = String(backupMode || 'sd_only').trim() || 'sd_only';
      const safeAssetMode = String(assetMode || 'sd_only').trim() || 'sd_only';
      return safeBackupMode !== 'sd_only' || safeAssetMode !== 'sd_only';
    }

    function setCloudOnlineVisibility(backupMode, assetMode) {
      const enabled = cloudModeNeedsOnline(backupMode, assetMode);
      cloudOnlineElements.forEach((element) => {
        element.hidden = !enabled;
      });
      return enabled;
    }

    function applyPageMode() {
      inventoryNavLink.hidden = isInventoryPage;
      ordersNavLink.hidden = isOrdersPage || isOrderDetailPage || isOrderFulfillPage;
      settingsNavLink.hidden = isSettingsPage;
      inventoryActionsSection.hidden = !isInventoryPage;
      inventoryToolsSection.hidden = !isInventoryPage;
      inventoryTableSection.hidden = !isInventoryPage;
      ordersSection.hidden = !isOrdersPage;
      orderDetailSection.hidden = !isOrderDetailPage;
      orderFulfillSection.hidden = !isOrderFulfillPage;
      settingsSection.hidden = !isSettingsPage;
    }

    function escapeHtml(value) {
      return String(value)
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#39;');
    }

    async function readJson(url, options = {}) {
      const response = await fetch(url, options);
      const data = await response.json().catch(() => ({}));
      if (!response.ok) {
        throw new Error(data.error || `Request failed (${response.status})`);
      }
      return data;
    }

    async function loadStatus() {
      const data = await readJson('/api/status');
      state.baseUrl = data.base_url || window.location.origin;
      const wifiSummary = data.wifi_connected
        ? `${data.wifi_ssid || 'connected'}${data.wifi_ip ? ` @ ${data.wifi_ip}` : ''}`
        : data.wifi_ap_active
          ? `AP ${data.wifi_ap_ssid || 'Stingray-Inventory'}`
          : data.wifi_setup_required
            ? 'setup required'
            : 'offline';
      hostInfo.textContent = `${state.baseUrl} | Wi-Fi: ${wifiSummary} | SD: ${data.sd_ready ? 'ready' : 'missing'} | Device: ${data.device_id || 'unknown'} | Time: ${data.time_source || 'unknown'}`;
      renderBranding({
        brand_name: data.brand_name || 'Stingray Inventory',
        brand_logo_ref: data.brand_logo_ref || '',
      });
    }

    function wifiNetworkLabel(network) {
      const parts = [network.ssid || 'Unnamed network'];
      if (Number.isFinite(Number(network.rssi))) {
        parts.push(`${network.rssi} dBm`);
      }
      if (network.auth) {
        parts.push(network.auth);
      }
      return parts.join(' | ');
    }

    function renderWifiNetworkOptions() {
      const preferredValue = wifiSsidEl.value.trim()
        || (state.wifiConfig && (state.wifiConfig.current_ssid || state.wifiConfig.saved_ssid || state.wifiConfig.effective_ssid))
        || '';
      const safeNetworks = Array.isArray(state.wifiNetworks) ? state.wifiNetworks : [];
      const hasPreferred = safeNetworks.some((network) => (network.ssid || '') === preferredValue);

      wifiNetworkSelectEl.innerHTML = '';

      const defaultOption = document.createElement('option');
      defaultOption.value = '';
      defaultOption.textContent = safeNetworks.length ? 'Choose a scanned network' : 'No scanned networks yet';
      wifiNetworkSelectEl.appendChild(defaultOption);

      for (const network of safeNetworks) {
        const option = document.createElement('option');
        option.value = network.ssid || '';
        option.textContent = wifiNetworkLabel(network);
        if ((network.ssid || '') === preferredValue) {
          option.selected = true;
        }
        wifiNetworkSelectEl.appendChild(option);
      }

      const manualOption = document.createElement('option');
      manualOption.value = '__manual__';
      manualOption.textContent = 'Enter network manually / hidden SSID';
      manualOption.selected = Boolean(preferredValue) && !hasPreferred;
      wifiNetworkSelectEl.appendChild(manualOption);
    }

    function renderWifiStatus(config) {
      const safeConfig = config || {};
      const lines = [
        `Connection: ${safeConfig.connected ? 'connected' : 'not connected'}`,
        `Current SSID: ${safeConfig.current_ssid || 'none'}`,
        `Current IP: ${safeConfig.current_ip || 'none'}`,
        `Signal: ${safeConfig.connected ? `${safeConfig.current_rssi} dBm` : 'n/a'}`,
        `Saved SSID: ${safeConfig.saved_ssid || 'none'}`,
        `Config Source: ${safeConfig.config_source || 'none'}`,
        `Mode: ${safeConfig.wifi_mode || 'unknown'}`,
      ];

      if (safeConfig.ap_active) {
        lines.push(`AP Fallback: ${safeConfig.ap_ssid || 'Stingray-Inventory'} ${safeConfig.ap_ip ? `(${safeConfig.ap_ip})` : ''}`.trim());
      }

      if (safeConfig.last_error) {
        lines.push(`Last Error: ${safeConfig.last_error}`);
      }

      wifiStatusOutput.textContent = lines.join('\n');
      wifiCaptionEl.textContent = safeConfig.saved_updated_at
        ? `Saved Wi-Fi settings updated at ${safeConfig.saved_updated_at}. Scan nearby networks, or type a hidden SSID manually.`
        : 'Scan nearby networks, save Wi-Fi credentials on the device, and connect without reflashing firmware.';
    }

    function applyWifiConfig(config) {
      state.wifiConfig = config || null;
      const safeConfig = config || {};
      if (!wifiSsidEl.value.trim() || safeConfig.saved_ssid) {
        wifiSsidEl.value = safeConfig.saved_ssid || safeConfig.current_ssid || safeConfig.effective_ssid || '';
      }
      wifiPasswordEl.value = '';
      renderWifiNetworkOptions();
      renderWifiStatus(safeConfig);
    }

    async function loadWifiConfig() {
      const data = await readJson('/api/wifi/config');
      applyWifiConfig(data);
    }

    async function scanWifiNetworks() {
      const data = await readJson('/api/wifi/scan');
      state.wifiNetworks = Array.isArray(data.networks) ? data.networks : [];
      renderWifiNetworkOptions();
      return state.wifiNetworks;
    }

    async function saveWifiSetup() {
      const ssid = wifiSsidEl.value.trim();
      if (!ssid) {
        throw new Error('Choose a scanned network or type an SSID manually.');
      }

      const params = new URLSearchParams();
      params.set('ssid', ssid);
      params.set('password', wifiPasswordEl.value);

      const data = await readJson('/api/wifi/config', {
        method: 'POST',
        body: params,
      });
      applyWifiConfig(data);
      return data;
    }

    async function forgetWifiSetup() {
      const data = await readJson('/api/wifi/forget', {
        method: 'POST',
      });
      state.wifiNetworks = [];
      applyWifiConfig(data);
      return data;
    }

    function resolveAssetUrl(value) {
      const text = String(value || '').trim();
      if (!text) {
        return '';
      }

      if (text.startsWith('/') || text.startsWith('http://') || text.startsWith('https://')) {
        return text;
      }

      return '';
    }

    function renderBranding(config) {
      const safeConfig = config || {};
      const brandText = safeConfig.brand_name || 'Stingray Inventory';
      const logoRef = safeConfig.brand_logo_ref || '';
      const logoUrl = resolveAssetUrl(logoRef);
      brandTitle.textContent = brandText;
      document.title = isSettingsPage
        ? `${brandText} Settings`
        : (isOrdersPage || isOrderDetailPage || isOrderFulfillPage)
          ? `${brandText} Orders`
          : brandText;

      if (logoUrl) {
        brandLogo.hidden = false;
        brandLogo.src = logoUrl;
        brandLogo.alt = `${brandText} logo`;
      } else {
        brandLogo.hidden = true;
        brandLogo.removeAttribute('src');
      }
    }

    function brandLogoSummary() {
      if (state.draftBrandLogoFile) {
        return `Selected logo: ${state.draftBrandLogoFile.name}. It will upload to the SD card when you save settings.`;
      }

      if (state.cloudConfig && state.cloudConfig.brand_logo_ref) {
        return `Saved logo ref: ${state.cloudConfig.brand_logo_ref}`;
      }

      return 'No brand logo selected. Upload stores the file on the SD card.';
    }

    function renderGoogleStatus(config) {
      const safeConfig = config || {};
      const lines = [
        `Auth: ${safeConfig.auth_status || 'unknown'}`,
        `Sync: ${safeConfig.sync_status || 'idle'}`,
        `Folder ID: ${safeConfig.folder_id || 'not resolved yet'}`,
        `Last Sync: ${safeConfig.last_sync_at || 'never'}`,
        `Local Snapshot: ${safeConfig.local_snapshot_at || 'not tracked yet'}`,
        `Last Remote Snapshot: ${safeConfig.last_synced_snapshot_at || 'none'}`,
        `Drive Scope: ${safeConfig.drive_scope || 'n/a'}`,
      ];

      if (safeConfig.user_code && safeConfig.verification_url) {
        lines.push(`Approve at: ${safeConfig.verification_url}`);
        lines.push(`Enter code: ${safeConfig.user_code}`);
      }

      if (safeConfig.last_error) {
        lines.push(`Last Error: ${safeConfig.last_error}`);
      }

      if (!safeConfig.sd_ready) {
        lines.push('SD status: missing or failed. Insert a replacement SD card, then reboot to restore files from Google Drive.');
      }

      googleStatusOutput.textContent = lines.join('\n');
    }

    function renderLogLines(target, lines, emptyText) {
      const safeLines = Array.isArray(lines) ? lines : [];
      target.textContent = safeLines.length ? safeLines.join('\n') : emptyText;
    }

    async function refreshLogs() {
      const deviceData = await readJson('/api/logs/device');
      renderLogLines(deviceLogOutput, deviceData.lines, 'No activity log entries yet.');
    }

    async function uploadBrandLogo() {
      if (!state.draftBrandLogoFile) {
        return state.cloudConfig && state.cloudConfig.brand_logo_ref ? state.cloudConfig.brand_logo_ref : '';
      }

      const formData = new FormData();
      formData.append('image', state.draftBrandLogoFile);
      const data = await readJson('/api/images/upload', {
        method: 'POST',
        body: formData,
      });

      state.draftBrandLogoFile = null;
      return data.image_ref || '';
    }

    function normalize(value) {
      return String(value || '').trim().toLowerCase();
    }

    function field(item, key) {
      return item[key] || '';
    }

    function matchesSearch(item, query) {
      const needle = normalize(query);
      if (!needle) {
        return true;
      }

      const haystack = [
        item.id,
        field(item, 'category'),
        field(item, 'part_name'),
        field(item, 'qr_code'),
        field(item, 'color'),
        field(item, 'material'),
        field(item, 'image_ref'),
      ].join(' ').toLowerCase();

      return haystack.includes(needle);
    }

    function filteredItems() {
      return state.items.filter((item) => {
        const categoryMatch = state.activeCategory === 'all' || normalize(item.category) === state.activeCategory;
        return categoryMatch && matchesSearch(item, state.search);
      });
    }

    function renderCaption(items) {
      const zeroCount = items.filter((item) => Number(item.qty) === 0).length;
      inventoryTitle.textContent = tabLabels[state.activeCategory];
      inventoryCaption.textContent = `${items.length} item(s) shown. ${zeroCount} out of stock. Search matches part name, category, part number, QR code, color, material, and image reference.`;
    }

    function syncTabs() {
      document.querySelectorAll('[data-category]').forEach((button) => {
        button.classList.toggle('active', button.dataset.category === state.activeCategory);
      });
    }

    function updatedAtScore(value) {
      const text = String(value || '').trim();
      const parsed = Date.parse(text);
      if (!Number.isNaN(parsed)) {
        return parsed;
      }

      const uptimeMatch = text.match(/^UPTIME\+(\d+)s$/);
      if (uptimeMatch) {
        return Number(uptimeMatch[1]) * 1000;
      }

      return 0;
    }

    function readRecentSelections() {
      try {
        const parsed = JSON.parse(window.localStorage.getItem(RECENT_SELECTIONS_KEY) || '{}');
        return {
          colors: Array.isArray(parsed.colors) ? parsed.colors : [],
          materials: Array.isArray(parsed.materials) ? parsed.materials : [],
        };
      } catch (error) {
        return { colors: [], materials: [] };
      }
    }

    function writeRecentSelections(value) {
      try {
        window.localStorage.setItem(RECENT_SELECTIONS_KEY, JSON.stringify(value));
      } catch (error) {
      }
    }

    function pushUniqueValue(list, value) {
      const trimmed = String(value || '').trim();
      if (!trimmed) {
        return;
      }

      const exists = list.some((entry) => normalize(entry) === normalize(trimmed));
      if (!exists) {
        list.push(trimmed);
      }
    }

    function recentOptionsFor(fieldName, storageKey) {
      const options = [];
      const recentSelections = readRecentSelections();
      (recentSelections[storageKey] || []).forEach((value) => {
        pushUniqueValue(options, value);
      });

      [...state.items]
        .sort((a, b) => updatedAtScore(b.updated_at) - updatedAtScore(a.updated_at))
        .forEach((item) => {
          pushUniqueValue(options, item[fieldName]);
        });

      return options.slice(0, 12);
    }

    function rememberRecentSelection(storageKey, value) {
      const recentSelections = readRecentSelections();
      const nextValues = [];
      pushUniqueValue(nextValues, value);
      (recentSelections[storageKey] || []).forEach((entry) => {
        pushUniqueValue(nextValues, entry);
      });
      recentSelections[storageKey] = nextValues.slice(0, 12);
      writeRecentSelections(recentSelections);
    }

    function applyCloudConfig(config) {
      state.cloudConfig = config || null;
      state.draftBrandLogoFile = null;
      const safeConfig = config || {};
      const backupMode = safeConfig.backup_mode || 'sd_only';
      const assetMode = safeConfig.asset_mode || 'sd_only';
      backupModeEl.value = backupMode;
      assetModeEl.value = assetMode;
      brandNameEl.value = safeConfig.brand_name || 'Stingray Inventory';
      cloudProviderEl.value = safeConfig.provider || 'google_drive';
      cloudLoginEmailEl.value = safeConfig.login_email || '';
      cloudFolderModeEl.value = safeConfig.mode || 'select_or_create';
      cloudFolderNameEl.value = safeConfig.folder_name || '';
      cloudFolderHintEl.value = safeConfig.folder_hint || '';
      googleClientIdEl.value = safeConfig.client_id || '';
      googleClientSecretEl.value = safeConfig.client_secret || '';
      brandLogoFileEl.value = '';
      brandLogoNoteEl.textContent = brandLogoSummary();
      renderBranding(safeConfig);
      renderGoogleStatus(safeConfig);
      const cloudEnabled = setCloudOnlineVisibility(backupMode, assetMode);
      googleAuthPollBtn.disabled = !cloudEnabled || (safeConfig.auth_status || '') !== 'pending';
      googleSyncBtn.disabled = !cloudEnabled || !safeConfig.auth_ready;
      googleRestoreBtn.disabled = !cloudEnabled || !safeConfig.auth_ready;
      googleDisconnectBtn.disabled = !cloudEnabled || (!safeConfig.auth_ready && (safeConfig.auth_status || '') !== 'pending');
      if (cloudEnabled) {
        cloudCaption.textContent = safeConfig.updated_at
          ? `Saved at ${safeConfig.updated_at}. Backup mode: ${backupMode}. Asset mode: ${assetMode}. Google uses drive.file scope, so use a Stingray-managed folder.`
          : 'Standalone SD mode is active. Switch backup or asset mode above to enable Google sync setup.';
      } else {
        cloudCaption.textContent = safeConfig.updated_at
          ? `Saved at ${safeConfig.updated_at}. Standalone SD mode is active for both backup and assets.`
          : 'Standalone mode keeps inventory, UI assets, logs, and branding on the SD card.';
      }
    }

    async function loadCloudConfig() {
      const data = await readJson('/api/cloud-config');
      applyCloudConfig(data);
    }

    async function saveCloudConfig() {
      let brandLogoRef = state.cloudConfig && state.cloudConfig.brand_logo_ref ? state.cloudConfig.brand_logo_ref : '';
      if (state.draftBrandLogoFile) {
        setStatus('Uploading brand logo to SD card...', 'ok');
        brandLogoRef = await uploadBrandLogo();
      }

      const params = new URLSearchParams();
      params.set('backup_mode', backupModeEl.value || 'sd_only');
      params.set('asset_mode', assetModeEl.value || 'sd_only');
      params.set('brand_name', brandNameEl.value.trim() || 'Stingray Inventory');
      params.set('brand_logo_ref', brandLogoRef);
      params.set('provider', cloudProviderEl.value || 'google_drive');
      params.set('login_email', cloudLoginEmailEl.value.trim());
      params.set('folder_name', cloudFolderNameEl.value.trim());
      params.set('folder_hint', cloudFolderHintEl.value.trim());
      params.set('mode', cloudFolderModeEl.value || 'select_or_create');
      params.set('client_id', googleClientIdEl.value.trim());
      params.set('client_secret', googleClientSecretEl.value.trim());

      const data = await readJson('/api/cloud-config', {
        method: 'POST',
        body: params,
      });

      applyCloudConfig(data);
    }

    async function startGoogleAuth() {
      const data = await readJson('/api/google-auth/start', {
        method: 'POST',
      });
      applyCloudConfig(data);
    }

    async function pollGoogleAuth() {
      const data = await readJson('/api/google-auth/poll', {
        method: 'POST',
      });
      applyCloudConfig(data);
    }

    async function disconnectGoogleAuth() {
      const data = await readJson('/api/google-auth/disconnect', {
        method: 'POST',
      });
      applyCloudConfig(data);
    }

    async function syncGoogleDrive() {
      const data = await readJson('/api/google-drive/sync', {
        method: 'POST',
      });
      applyCloudConfig(data);
    }

    async function restoreGoogleDrive() {
      const data = await readJson('/api/google-drive/restore', {
        method: 'POST',
      });
      applyCloudConfig(data);
      await refreshItems();
    }

    function createDraftItem() {
      return {
        id: '',
        category: 'part',
        part_name: '',
        color: '',
        material: '',
        bom_product: '',
        bom_qty: '0',
        qty: '0',
        qr_code: '',
        image_ref: '',
        color_mode: 'select',
        material_mode: 'select',
      };
    }

    function draftImageSummary() {
      if (state.draftImageFile) {
        return `Selected image: ${state.draftImageFile.name}. File will upload to the SD card when you save the item.`;
      }

      if (state.draftItem && state.draftItem.image_ref) {
        return `Saved image ref: ${state.draftItem.image_ref}`;
      }

      return 'No image selected. Uploaded images are stored on the SD card and referenced automatically.';
    }

    function shouldShowDraftRow() {
      return Boolean(state.draftItem) && state.activeCategory === 'all';
    }

    function focusDraftField(selector = '[data-draft-field="id"]') {
      window.setTimeout(() => {
        const field = itemsBody.querySelector(selector);
        if (field) {
          field.focus();
          if (typeof field.select === 'function') {
            field.select();
          }
        }
      }, 0);
    }

    function openDraftRow() {
      state.activeCategory = 'all';
      state.draftItem = state.draftItem || createDraftItem();
      syncTabs();
      renderItems(filteredItems());
      focusDraftField();
    }

    function closeDraftRow() {
      state.draftItem = null;
      state.draftImageFile = null;
      renderItems(filteredItems());
    }

    function draftPayload() {
      if (!state.draftItem) {
        return null;
      }

      const payload = {
        id: String(state.draftItem.id || '').trim(),
        category: normalize(state.draftItem.category) || 'part',
        part_name: String(state.draftItem.part_name || '').trim(),
        color: String(state.draftItem.color || '').trim(),
        material: String(state.draftItem.material || '').trim(),
        bom_product: String(state.draftItem.bom_product || '').trim(),
        bom_qty: String(state.draftItem.bom_qty || '').trim(),
        qty: String(state.draftItem.qty || '').trim(),
        qr_code: String(state.draftItem.qr_code || '').trim(),
        image_ref: String(state.draftItem.image_ref || '').trim(),
      };

      if (payload.category !== 'part') {
        payload.bom_product = '';
        payload.bom_qty = '0';
      }

      if (!payload.bom_qty) {
        payload.bom_qty = '0';
      }
      if (!payload.qty) {
        payload.qty = '0';
      }

      return payload;
    }

    async function uploadDraftImage() {
      if (!state.draftImageFile) {
        return state.draftItem ? state.draftItem.image_ref : '';
      }

      const formData = new FormData();
      formData.append('image', state.draftImageFile);

      const data = await readJson('/api/images/upload', {
        method: 'POST',
        body: formData,
      });

      if (state.draftItem) {
        state.draftItem.image_ref = data.image_ref || '';
      }
      state.draftImageFile = null;
      return data.image_ref || '';
    }

    function matchingOption(options, value) {
      const wanted = normalize(value);
      if (!wanted) {
        return '';
      }

      const match = options.find((option) => normalize(option) === wanted);
      return match || '';
    }

    function categoryOptionsHtml(selectedValue) {
      return CATEGORY_OPTIONS.map((option) => {
        const selected = normalize(option.value) === normalize(selectedValue) ? ' selected' : '';
        return `<option value="${escapeHtml(option.value)}"${selected}>${escapeHtml(option.label)}</option>`;
      }).join('');
    }

    function recentSelectHtml(fieldName, storageKey, placeholder, customPlaceholder) {
      const options = recentOptionsFor(fieldName, storageKey);
      const currentValue = state.draftItem ? state.draftItem[fieldName] : '';
      const matched = matchingOption(options, currentValue);
      const modeKey = `${fieldName}_mode`;
      const useCustom = Boolean(state.draftItem && state.draftItem[modeKey] === 'custom') || (Boolean(currentValue) && !matched);
      const selectedValue = useCustom ? CUSTOM_OPTION_VALUE : matched;
      const optionsHtml = options.map((option) => {
        const selected = normalize(option) === normalize(selectedValue) ? ' selected' : '';
        return `<option value="${escapeHtml(option)}"${selected}>${escapeHtml(option)}</option>`;
      }).join('');

      return `
        <div class="cell-stack">
          <select data-draft-recent="${fieldName}">
            <option value="">${escapeHtml(placeholder)}</option>
            ${optionsHtml}
            <option value="${CUSTOM_OPTION_VALUE}"${useCustom ? ' selected' : ''}>Add another...</option>
          </select>
          <input
            data-draft-custom="${fieldName}"
            type="text"
            placeholder="${escapeHtml(customPlaceholder)}"
            value="${useCustom ? escapeHtml(currentValue) : ''}"
            ${useCustom ? '' : 'hidden'}
          >
        </div>
      `;
    }

    function renderDraftRow() {
      if (!state.draftItem) {
        return null;
      }

      const row = document.createElement('tr');
      row.className = 'draft-row';
      row.innerHTML = `
        <td>
          <div class="cell-stack">
            <input data-draft-field="id" type="text" placeholder="Part number" value="${escapeHtml(state.draftItem.id)}" required>
          </div>
        </td>
        <td>
          <div class="cell-stack">
            <select data-draft-category>
              ${categoryOptionsHtml(state.draftItem.category)}
            </select>
            <div class="cell-note">Supported categories: product, part, kit.</div>
          </div>
        </td>
        <td>
          <div class="cell-stack">
            <input data-draft-field="part_name" type="text" placeholder="Part name" value="${escapeHtml(state.draftItem.part_name)}" required>
          </div>
        </td>
        <td>${recentSelectHtml('color', 'colors', 'Select color', 'Add new color')}</td>
        <td>${recentSelectHtml('material', 'materials', 'Select material', 'Add new material')}</td>
        <td>
          <div class="cell-stack">
            <input data-draft-field="qty" type="number" min="0" value="${escapeHtml(state.draftItem.qty)}">
          </div>
        </td>
        <td>
          <div class="cell-stack">
            <input data-draft-image-file type="file" accept="image/*">
            <div class="cell-note">${escapeHtml(draftImageSummary())}</div>
          </div>
        </td>
        <td>
          <div class="cell-stack">
            <input data-draft-field="qr_code" type="text" placeholder="QR or UPC value" value="${escapeHtml(state.draftItem.qr_code)}">
            <div class="cell-note">Stored reference code. Printed QR still uses the item link.</div>
          </div>
        </td>
        <td>
          <div class="inline-actions">
            <button type="button" data-draft-action="save">Save</button>
            <button type="button" class="secondary" data-draft-action="cancel">Cancel</button>
          </div>
        </td>
      `;
      return row;
    }

    function openQrPreview(itemId, link, qrSrc) {
      activeQrPreview = { itemId, link, qrSrc };
      qrModalImage.src = qrSrc;
      qrModalImage.alt = `QR code for item ${itemId}`;
      qrModalLink.href = link;
      qrModalLink.textContent = link;
      qrModal.hidden = false;
      document.body.classList.add('qr-modal-open');
      qrModalClose.focus();
    }

    function openImagePreview(itemId, imageSrc, imageRef) {
      activeImagePreview = { itemId, imageSrc, imageRef };
      imageModalImage.src = imageSrc;
      imageModalImage.alt = `Image for item ${itemId}`;
      imageModalRef.textContent = imageRef;
      imageModal.hidden = false;
      document.body.classList.add('qr-modal-open');
      imageModalClose.focus();
    }

    function closeQrPreview() {
      if (qrModal.hidden) {
        return;
      }

      qrModal.hidden = true;
      document.body.classList.remove('qr-modal-open');
      qrModalImage.removeAttribute('src');
      qrModalLink.removeAttribute('href');
      qrModalLink.textContent = '';
      activeQrPreview = null;
    }

    function closeImagePreview() {
      if (imageModal.hidden) {
        return;
      }

      imageModal.hidden = true;
      document.body.classList.remove('qr-modal-open');
      imageModalImage.removeAttribute('src');
      imageModalRef.textContent = '';
      activeImagePreview = null;
    }

    function printQrPreview() {
      if (!activeQrPreview) {
        return;
      }

      const safeId = escapeHtml(activeQrPreview.itemId || 'Item QR');
      const safeLink = escapeHtml(activeQrPreview.link);
      const safeQrSrc = escapeHtml(activeQrPreview.qrSrc);
      const printWindow = window.open('', '_blank', 'width=520,height=720');

      if (!printWindow) {
        setStatus('Allow pop-ups to print QR codes.', 'error');
        return;
      }

      printWindow.document.write(`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${safeId}</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 1.25rem;
      font-family: "Segoe UI", Tahoma, sans-serif;
      color: #1b2f3f;
      background: #ffffff;
    }
    .print-card {
      max-width: 360px;
      margin: 0 auto;
      text-align: center;
      border: 1px solid #d5dde2;
      border-radius: 18px;
      padding: 1rem;
    }
    h1 {
      margin: 0 0 0.85rem;
      font-size: 1.2rem;
    }
    img {
      display: block;
      width: 100%;
      max-width: 260px;
      height: auto;
      margin: 0 auto 0.9rem;
      padding: 0.8rem;
      border: 1px solid #d5dde2;
      border-radius: 16px;
      background: #fff;
      image-rendering: pixelated;
    }
    .link {
      margin: 0;
      font-size: 0.85rem;
      word-break: break-all;
    }
    @media print {
      body {
        padding: 0;
      }
      .print-card {
        border: 0;
      }
    }
  </style>
</head>
<body>
  <main class="print-card">
    <h1>${safeId}</h1>
    <img id="print-qr" src="${safeQrSrc}" alt="QR code for item ${safeId}">
    <p class="link">${safeLink}</p>
  </main>
</body>
</html>`);
      printWindow.document.close();

      const runPrint = () => {
        printWindow.focus();
        printWindow.print();
      };

      printWindow.addEventListener('load', () => {
        const image = printWindow.document.getElementById('print-qr');
        if (image && !image.complete) {
          image.addEventListener('load', runPrint, { once: true });
          image.addEventListener('error', runPrint, { once: true });
          return;
        }
        runPrint();
      }, { once: true });
    }

    function toWholeNumber(value, fallback = 0) {
      const parsed = Number.parseInt(String(value || '').trim(), 10);
      if (!Number.isFinite(parsed) || parsed < 0) {
        return fallback;
      }
      return parsed;
    }

    function orderDateStamp() {
      const now = new Date();
      const year = String(now.getFullYear());
      const month = String(now.getMonth() + 1).padStart(2, '0');
      const day = String(now.getDate()).padStart(2, '0');
      return `${year}${month}${day}`;
    }

    function normalizeOrderNumber(value) {
      return String(value || '').trim().toUpperCase();
    }

    function createOrderLine() {
      return {
        lineId: `${Date.now()}-${Math.floor(Math.random() * 100000)}`,
        itemId: '',
        needed: 1,
      };
    }

    function ensureOrderLineHeadroom(order) {
      if (!order || !Array.isArray(order.lines)) {
        return;
      }
      if (order.lines.length > MAX_ORDER_LINE_COUNT) {
        order.lines = order.lines.slice(0, MAX_ORDER_LINE_COUNT);
      }
      if (!order.lines.length) {
        order.lines = [createOrderLine()];
      }
    }

    function sanitizeOrderLine(rawLine) {
      return {
        lineId: String(rawLine && rawLine.lineId ? rawLine.lineId : `${Date.now()}-${Math.floor(Math.random() * 100000)}`),
        itemId: String(rawLine && rawLine.itemId ? rawLine.itemId : '').trim(),
        needed: toWholeNumber(rawLine && rawLine.needed, 1),
      };
    }

    function sanitizeOrder(rawOrder) {
      const orderNumber = normalizeOrderNumber(rawOrder && rawOrder.order_number);
      if (!orderNumber) {
        return null;
      }

      const lines = Array.isArray(rawOrder.lines)
        ? rawOrder.lines.map((line) => sanitizeOrderLine(line))
        : [];

      const order = {
        order_number: orderNumber,
        created_at: String(rawOrder.created_at || new Date().toISOString()),
        updated_at: String(rawOrder.updated_at || rawOrder.created_at || new Date().toISOString()),
        lines,
      };
      ensureOrderLineHeadroom(order);
      return order;
    }

    async function loadOrders() {
      const data = await readJson('/api/orders');
      const parsed = Array.isArray(data.orders) ? data.orders : [];
      const orders = [];

      for (const rawOrder of parsed) {
        const order = sanitizeOrder(rawOrder);
        if (!order) {
          continue;
        }
        orders.push(order);
        if (orders.length >= MAX_ORDER_COUNT) {
          break;
        }
      }

      orders.sort((a, b) => Date.parse(b.updated_at || '') - Date.parse(a.updated_at || ''));
      return orders;
    }

    async function saveOrders() {
      state.orders.sort((a, b) => Date.parse(b.updated_at || '') - Date.parse(a.updated_at || ''));
      if (state.orders.length > MAX_ORDER_COUNT) {
        state.orders = state.orders.slice(0, MAX_ORDER_COUNT);
      }

      const payload = JSON.stringify({ orders: state.orders });
      const params = new URLSearchParams();
      params.set('payload', payload);
      await readJson('/api/orders', {
        method: 'POST',
        body: params,
      });
    }

    function findOrder(orderNumber) {
      const normalizedOrderNumber = normalizeOrderNumber(orderNumber);
      if (!normalizedOrderNumber) {
        return null;
      }
      return state.orders.find((order) => normalizeOrderNumber(order.order_number) === normalizedOrderNumber) || null;
    }

    function currentOrder() {
      return findOrder(state.currentOrderNumber);
    }

    function nextOrderNumber() {
      const dateStamp = orderDateStamp();
      const prefix = `ORD-${dateStamp}-`;
      let counter = 1;

      for (const order of state.orders) {
        const orderNumber = normalizeOrderNumber(order.order_number);
        if (!orderNumber.startsWith(prefix)) {
          continue;
        }

        const suffix = orderNumber.substring(prefix.length);
        const parsed = Number.parseInt(suffix, 10);
        if (Number.isFinite(parsed) && parsed >= counter) {
          counter = parsed + 1;
        }
      }

      let candidate = '';

      while (counter < 100000) {
        candidate = `${prefix}${String(counter).padStart(4, '0')}`;
        if (!findOrder(candidate)) {
          break;
        }
        counter += 1;
      }

      return candidate;
    }

    function itemById(itemId) {
      const needle = String(itemId || '').trim();
      if (!needle) {
        return null;
      }
      return state.items.find((item) => String(item.id || '').trim() === needle) || null;
    }

    function orderItemOptionsHtml(selectedId) {
      const safeSelectedId = String(selectedId || '').trim();
      const options = [...state.items].sort((a, b) => {
        const categoryCompare = normalize(a.category || '').localeCompare(normalize(b.category || ''));
        if (categoryCompare !== 0) {
          return categoryCompare;
        }
        return normalize(a.id || '').localeCompare(normalize(b.id || ''));
      });

      const optionHtml = options.map((item) => {
        const id = String(item.id || '').trim();
        const selected = id === safeSelectedId ? ' selected' : '';
        const category = item.category_label || item.category || 'item';
        const stock = toWholeNumber(item.qty, 0);
        const label = `${id} - ${item.part_name || 'Unnamed'} [${category}] stock ${stock}`;
        return `<option value="${escapeHtml(id)}"${selected}>${escapeHtml(label)}</option>`;
      }).join('');
      const selectedExists = safeSelectedId
        ? options.some((item) => String(item.id || '').trim() === safeSelectedId)
        : true;
      const selectedFallback = safeSelectedId && !selectedExists
        ? `<option value="${escapeHtml(safeSelectedId)}" selected>${escapeHtml(`${safeSelectedId} - Missing from inventory`)}</option>`
        : '';

      return `<option value="">Select part / product / kit</option>${selectedFallback}${optionHtml}`;
    }

    function buildOrderLineMetrics(lines) {
      const allocatedByItem = {};
      const metrics = {};

      for (const line of lines) {
        const needed = toWholeNumber(line.needed, 0);
        const selectedItemId = String(line.itemId || '').trim();
        if (!selectedItemId) {
          metrics[line.lineId] = {
            item: null,
            selected: false,
            missing: false,
            stock: 0,
            needed,
            outOfStock: false,
            itemsShort: 0,
            inStockUnits: 0,
          };
          continue;
        }

        const item = itemById(selectedItemId);
        if (!item) {
          metrics[line.lineId] = {
            item: null,
            selected: true,
            missing: true,
            stock: 0,
            needed,
            outOfStock: needed > 0,
            itemsShort: needed,
            inStockUnits: 0,
          };
          continue;
        }

        const itemId = String(item.id || '').trim();
        const stock = toWholeNumber(item.qty, 0);
        const alreadyAllocated = allocatedByItem[itemId] || 0;
        const available = Math.max(0, stock - alreadyAllocated);
        const itemsShort = Math.max(0, needed - available);
        const inStockUnits = Math.max(0, needed - itemsShort);

        allocatedByItem[itemId] = alreadyAllocated + needed;
        metrics[line.lineId] = {
          item,
          selected: true,
          missing: false,
          stock,
          needed,
          outOfStock: needed > 0 && stock <= 0,
          itemsShort,
          inStockUnits,
        };
      }

      return metrics;
    }

    function summarizeOrder(order) {
      const lines = Array.isArray(order.lines) ? order.lines : [];
      const metricsByLine = buildOrderLineMetrics(lines);
      let inStock = 0;
      let outOfStock = 0;
      let itemsShort = 0;

      for (const line of lines) {
        const metrics = metricsByLine[line.lineId];
        if (!metrics) {
          continue;
        }

        inStock += metrics.inStockUnits;
        itemsShort += metrics.itemsShort;
        if (metrics.selected && metrics.outOfStock && metrics.needed > 0) {
          outOfStock += 1;
        }
      }

      return {
        metricsByLine,
        inStock,
        outOfStock,
        itemsShort,
      };
    }

    function openOrder(orderNumber) {
      window.location.href = `/orders/view?order=${encodeURIComponent(orderNumber)}`;
    }

    function openOrderFulfillment(orderNumber) {
      window.location.href = `/orders/fulfill?order=${encodeURIComponent(orderNumber)}`;
    }

    function orderFulfillmentRequirements(order) {
      const requirementsById = {};
      const lines = Array.isArray(order && order.lines) ? order.lines : [];

      for (const line of lines) {
        const itemId = String(line && line.itemId ? line.itemId : '').trim();
        const needed = toWholeNumber(line && line.needed, 0);
        if (!itemId || needed <= 0) {
          continue;
        }

        if (!requirementsById[itemId]) {
          requirementsById[itemId] = {
            itemId,
            needed: 0,
          };
        }
        requirementsById[itemId].needed += needed;
      }

      const requirements = Object.values(requirementsById).map((entry) => {
        const item = itemById(entry.itemId);
        const stock = item ? toWholeNumber(item.qty, 0) : 0;
        const shortage = Math.max(0, entry.needed - stock);
        return {
          itemId: entry.itemId,
          item,
          stock,
          needed: entry.needed,
          shortage,
        };
      });

      requirements.sort((a, b) => normalize(a.itemId).localeCompare(normalize(b.itemId)));
      return requirements;
    }

    function orderFulfillmentPlan(requirements) {
      return requirements
        .map((entry) => `${String(entry.itemId || '').trim()}|${toWholeNumber(entry.needed, 0)}`)
        .filter((line) => line !== '|0')
        .join('\n');
    }

    function renderOrdersListSummary() {
      ordersSummary.textContent =
        `Orders: ${state.orders.length}\n` +
        `Order slots free: ${Math.max(0, MAX_ORDER_COUNT - state.orders.length)}\n` +
        `Mode: Large orders only`;
    }

    function renderOrdersList() {
      if (!isOrdersPage) {
        return;
      }

      ordersListBody.innerHTML = '';
      if (!state.orders.length) {
        ordersListBody.innerHTML = '<tr><td colspan="5">No large orders yet. Select Add Order to create one.</td></tr>';
        renderOrdersListSummary();
        return;
      }

      for (const order of state.orders) {
        const summary = summarizeOrder(order);
        const row = document.createElement('tr');
        row.innerHTML = `
          <td>${escapeHtml(order.order_number)}</td>
          <td>${summary.inStock}</td>
          <td>${summary.outOfStock}</td>
          <td>${summary.itemsShort}</td>
          <td><a href="/orders/view?order=${encodeURIComponent(order.order_number)}">View Order</a></td>
        `;
        ordersListBody.appendChild(row);
      }

      renderOrdersListSummary();
    }

    function renderOrderDetail() {
      if (!isOrderDetailPage) {
        return;
      }

      const order = currentOrder();
      if (!order) {
        orderFulfillBtn.disabled = true;
        orderDetailTitle.textContent = 'Order Not Found';
        orderDetailBody.innerHTML = '<tr><td colspan="6">Order not found. Go back and create a new order.</td></tr>';
        orderDetailSummary.textContent = 'This order number does not exist in device SD storage.';
        return;
      }

      orderFulfillBtn.disabled = false;
      ensureOrderLineHeadroom(order);
      orderDetailTitle.textContent = `Order ${order.order_number}`;
      const summary = summarizeOrder(order);
      orderDetailBody.innerHTML = '';

      for (const line of order.lines) {
        const metrics = summary.metricsByLine[line.lineId];
        const row = document.createElement('tr');
        row.dataset.orderLineId = line.lineId;
        const removeDisabled = order.lines.length <= 1 ? ' disabled' : '';
        const hasSelection = Boolean(metrics && metrics.selected);
        const outOfStockText = hasSelection ? (metrics.outOfStock ? 'Yes' : 'No') : '-';
        const outOfStockClass = hasSelection && metrics.outOfStock ? 'orders-shortage' : '';
        const shortClass = hasSelection
          ? (metrics.itemsShort > 0 ? 'orders-shortage' : 'orders-available')
          : '';
        const stockText = hasSelection ? metrics.stock : '-';
        const shortText = hasSelection ? metrics.itemsShort : '-';

        row.innerHTML = `
          <td>
            <select class="orders-item-select" data-order-item>
              ${orderItemOptionsHtml(line.itemId)}
            </select>
          </td>
          <td>${stockText}</td>
          <td>
            <input
              class="orders-needed-input"
              data-order-needed
              type="number"
              min="0"
              step="1"
              value="${metrics ? metrics.needed : toWholeNumber(line.needed, 0)}"
            >
          </td>
          <td class="${outOfStockClass}">${outOfStockText}</td>
          <td class="${shortClass}">${shortText}</td>
          <td><button type="button" class="secondary" data-order-remove${removeDisabled}>Remove</button></td>
        `;
        orderDetailBody.appendChild(row);
      }

      const unselectedLines = order.lines.filter((line) => !String(line && line.itemId ? line.itemId : '').trim()).length;

      orderDetailSummary.textContent =
        `Order: ${order.order_number}\n` +
        `In stock units: ${summary.inStock}\n` +
        `Out of stock items: ${summary.outOfStock}\n` +
        `Items short: ${summary.itemsShort}\n` +
        `Unselected lines: ${unselectedLines}\n` +
        `Updated: ${order.updated_at}`;
    }

    function renderOrderFulfillment() {
      if (!isOrderFulfillPage) {
        return;
      }

      const order = currentOrder();
      if (!order) {
        orderFulfillTitle.textContent = 'Order Not Found';
        orderFulfillBody.innerHTML = '<tr><td colspan="6">Order not found. Go back to Orders and open another order.</td></tr>';
        orderFulfillSummary.textContent = 'This order number does not exist in device SD storage.';
        orderFulfillConfirmBtn.disabled = true;
        return;
      }

      const requirements = orderFulfillmentRequirements(order);
      const shortageCount = requirements.filter((entry) => entry.shortage > 0).length;
      const totalNeeded = requirements.reduce((sum, entry) => sum + entry.needed, 0);
      const totalStock = requirements.reduce((sum, entry) => sum + entry.stock, 0);
      const totalShort = requirements.reduce((sum, entry) => sum + entry.shortage, 0);
      const canFulfill = requirements.length > 0 && shortageCount === 0;

      orderFulfillTitle.textContent = `Fulfill ${order.order_number}`;
      orderFulfillBody.innerHTML = '';

      if (!requirements.length) {
        orderFulfillBody.innerHTML = '<tr><td colspan="6">No selected items yet. Add selected items on the order page first.</td></tr>';
      } else {
        for (const entry of requirements) {
          const row = document.createElement('tr');
          const shortClass = entry.shortage > 0 ? 'orders-shortage' : 'orders-available';
          const safeItem = entry.item || null;
          row.innerHTML = `
            <td>${escapeHtml(entry.itemId)}</td>
            <td>${escapeHtml(safeItem ? (safeItem.part_name || '') : 'Missing from inventory')}</td>
            <td>${escapeHtml(safeItem ? (safeItem.category_label || safeItem.category || '') : '-')}</td>
            <td>${entry.stock}</td>
            <td>${entry.needed}</td>
            <td class="${shortClass}">${entry.shortage}</td>
          `;
          orderFulfillBody.appendChild(row);
        }
      }

      orderFulfillSummary.textContent =
        `Order: ${order.order_number}\n` +
        `Selected items: ${requirements.length}\n` +
        `Total stock shown: ${totalStock}\n` +
        `Total needed: ${totalNeeded}\n` +
        `Items short: ${totalShort}`;

      orderFulfillConfirmBtn.disabled = !canFulfill;
      orderFulfillConfirmBtn.title = canFulfill
        ? 'Remove these quantities from inventory and close this order'
        : 'Cannot fulfill while there are no selected items or stock is short';
    }

    function renderItems(items) {
      itemsBody.innerHTML = '';
      renderCaption(items);

      if (shouldShowDraftRow()) {
        const draftRow = renderDraftRow();
        if (draftRow) {
          itemsBody.appendChild(draftRow);
        }
      }

      if (!items.length) {
        if (!shouldShowDraftRow()) {
          itemsBody.innerHTML = '<tr><td colspan="9">No items match the current tab and search.</td></tr>';
        }
        return;
      }

      for (const item of items) {
        const row = document.createElement('tr');
        if (Number(item.qty) === 0) {
          row.className = 'stock-zero';
        }

        const link = item.qr_link || `${state.baseUrl}/item?id=${encodeURIComponent(item.id)}`;
        const qrSrc = `/qr.svg?data=${encodeURIComponent(link)}`;
        const imageSrc = resolveAssetUrl(item.image_ref || '');
        row.innerHTML = `
          <td>${escapeHtml(item.id)}</td>
          <td><span class="meta-chip">${escapeHtml(item.category_label || item.category)}</span></td>
          <td>${escapeHtml(item.part_name || '')}</td>
          <td>${escapeHtml(item.color || '')}</td>
          <td>${escapeHtml(item.material || '')}</td>
          <td><span class="qty-pill">${item.qty}</span></td>
          <td class="asset-cell">
            ${imageSrc ? `
              <button
                type="button"
                class="asset-trigger image-trigger"
                data-item-id="${escapeHtml(item.id)}"
                data-image-src="${escapeHtml(imageSrc)}"
                data-image-ref="${escapeHtml(item.image_ref || '')}"
                aria-label="Preview image for ${escapeHtml(item.id)}"
                title="Preview item image"
              >
                <img class="asset-preview" src="${escapeHtml(imageSrc)}" alt="Image for item ${escapeHtml(item.id)}" loading="lazy">
              </button>
            ` : '<span class="small">-</span>'}
          </td>
          <td class="asset-cell">
            <button
              type="button"
              class="asset-trigger qr-trigger"
              data-item-id="${escapeHtml(item.id)}"
              data-qr-link="${escapeHtml(link)}"
              data-qr-src="${escapeHtml(qrSrc)}"
              aria-label="Preview QR code for ${escapeHtml(item.id)}"
              title="Preview and print QR code"
            >
              <img class="qr-preview" src="${qrSrc}" alt="QR code for item ${item.id}" loading="lazy">
            </button>
          </td>
          <td>
            <a href="/item?id=${encodeURIComponent(item.id)}">Open</a>
          </td>
        `;
        itemsBody.appendChild(row);
      }
    }

    async function refreshItems() {
      const payload = await readJson('/api/items');
      state.items = payload.items || [];
      if (isOrdersPage) {
        renderOrdersList();
        return;
      }
      if (isOrderDetailPage) {
        renderOrderDetail();
        return;
      }
      if (isOrderFulfillPage) {
        renderOrderFulfillment();
        return;
      }
      syncTabs();
      renderItems(filteredItems());
    }

    async function saveDraftItem() {
      const draft = draftPayload();
      if (!draft) {
        return;
      }

      if (!draft.id) {
        setStatus('Part number is required.', 'error');
        focusDraftField('[data-draft-field="id"]');
        return;
      }

      if (!draft.part_name) {
        setStatus('Part name is required.', 'error');
        focusDraftField('[data-draft-field="part_name"]');
        return;
      }

      if (state.draftImageFile) {
        setStatus('Uploading image to SD card...', 'ok');
        draft.image_ref = await uploadDraftImage();
      }

      const params = new URLSearchParams();
      params.set('id', draft.id);
      params.set('category', draft.category);
      params.set('part_name', draft.part_name);
      params.set('qr_code', draft.qr_code);
      params.set('color', draft.color);
      params.set('material', draft.material);
      params.set('image_ref', draft.image_ref);
      params.set('bom_product', draft.bom_product);
      params.set('bom_qty', draft.bom_qty || '0');
      params.set('qty', draft.qty || '0');

      await readJson('/api/items/add', {
        method: 'POST',
        body: params,
      });

      rememberRecentSelection('colors', draft.color);
      rememberRecentSelection('materials', draft.material);
      state.draftItem = null;
      await refreshItems();
      setStatus(`Item ${draft.id} added.`, 'ok');
    }

    async function removeItem(event) {
      event.preventDefault();
      const id = document.getElementById('remove-id').value.trim();
      if (!id) return;
      if (!confirm(`Remove item ${id}?`)) return;

      const params = new URLSearchParams();
      params.set('id', id);

      await readJson('/api/items/remove', {
        method: 'POST',
        body: params,
      });

      document.getElementById('remove-form').reset();
      await refreshItems();
      setStatus(`Item ${id} removed.`, 'ok');
    }

    function setCategory(category) {
      state.activeCategory = category;
      syncTabs();
      renderItems(filteredItems());
    }

    async function boot() {
      applyPageMode();
      try {
        await loadStatus();
        if (isOrdersPage || isOrderDetailPage || isOrderFulfillPage) {
          state.orders = await loadOrders();
          state.currentOrderNumber = normalizeOrderNumber(orderQueryNumber);
        }
        if (isSettingsPage) {
          let wifiError = '';
          let cloudError = '';
          let logError = '';
          try {
            await loadWifiConfig();
          } catch (error) {
            wifiError = error.message;
          }
          try {
            await scanWifiNetworks();
          } catch (error) {
            if (!wifiError) {
              wifiError = error.message;
            }
          }
          try {
            await loadCloudConfig();
          } catch (error) {
            cloudError = error.message;
          }
          try {
            await refreshLogs();
          } catch (error) {
            logError = error.message;
          }
          setStatus(
            wifiError || cloudError || logError
              ? `Settings loaded with issues. ${wifiError ? `Wi-Fi wizard unavailable: ${wifiError}. ` : ''}${cloudError ? `Cloud config unavailable: ${cloudError}. ` : ''}${logError ? `Activity log unavailable: ${logError}` : ''}`.trim()
              : 'Settings loaded.',
            wifiError || cloudError || logError ? 'error' : 'ok'
          );
        } else {
          await refreshItems();
          if (isOrderDetailPage) {
            setStatus(currentOrder() ? `Order ${state.currentOrderNumber} loaded.` : 'Order not found.', currentOrder() ? 'ok' : 'error');
          } else if (isOrderFulfillPage) {
            setStatus(currentOrder() ? `Fulfillment review loaded for ${state.currentOrderNumber}.` : 'Order not found.', currentOrder() ? 'ok' : 'error');
          } else if (isOrdersPage) {
            setStatus('Orders loaded.', 'ok');
          } else {
            setStatus('Inventory loaded.', 'ok');
          }
        }
      } catch (error) {
        setStatus(error.message, 'error');
      }

      if (isSettingsPage) {
        wifiNetworkSelectEl.addEventListener('change', () => {
          const selected = wifiNetworkSelectEl.value || '';
          if (selected && selected !== '__manual__') {
            wifiSsidEl.value = selected;
          }
        });
        wifiScanBtn.addEventListener('click', async () => {
          try {
            const networks = await scanWifiNetworks();
            setStatus(`Found ${networks.length} Wi-Fi network(s).`, 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        wifiSaveBtn.addEventListener('click', async () => {
          try {
            const data = await saveWifiSetup();
            await loadStatus();
            setStatus(
              data.connected
                ? `Wi-Fi connected to ${data.current_ssid || data.saved_ssid || 'the selected network'}. Reconnect on ${data.current_ip || 'the new network address'} after AP shutdown.`
                : `Wi-Fi settings saved for ${data.saved_ssid || 'the selected network'}, but the device stayed in AP mode. Check the password or signal and try again.`,
              data.connected ? 'ok' : 'error'
            );
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        wifiForgetBtn.addEventListener('click', async () => {
          if (!confirm('Forget the saved Wi-Fi network on this device?')) {
            return;
          }
          try {
            await forgetWifiSetup();
            await loadStatus();
            setStatus('Saved Wi-Fi settings cleared from this device.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        saveCloudBtn.addEventListener('click', async () => {
          try {
            await saveCloudConfig();
            await loadStatus();
            setStatus('Storage and branding settings saved to device storage.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        googleAuthStartBtn.addEventListener('click', async () => {
          try {
            await saveCloudConfig();
            await startGoogleAuth();
            setStatus('Google login started. Open the link shown in the Google status box and enter the code.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        googleAuthPollBtn.addEventListener('click', async () => {
          try {
            await pollGoogleAuth();
            await loadStatus();
            setStatus('Google login status checked.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        googleSyncBtn.addEventListener('click', async () => {
          try {
            await saveCloudConfig();
            await syncGoogleDrive();
            await loadStatus();
            setStatus('Google Drive sync completed.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        googleRestoreBtn.addEventListener('click', async () => {
          try {
            await saveCloudConfig();
            await restoreGoogleDrive();
            await loadStatus();
            setStatus('Google Drive restore completed.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        googleDisconnectBtn.addEventListener('click', async () => {
          try {
            await disconnectGoogleAuth();
            await loadStatus();
            setStatus('Google Drive link removed from this device.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        refreshLogsBtn.addEventListener('click', async () => {
          try {
            await refreshLogs();
            setStatus('Activity log refreshed.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        brandLogoFileEl.addEventListener('change', () => {
          state.draftBrandLogoFile = brandLogoFileEl.files && brandLogoFileEl.files[0] ? brandLogoFileEl.files[0] : null;
          brandLogoNoteEl.textContent = brandLogoSummary();
        });
        backupModeEl.addEventListener('change', () => {
          const enabled = setCloudOnlineVisibility(backupModeEl.value, assetModeEl.value);
          cloudCaption.textContent = enabled
            ? 'Google sync options are visible. Save settings to apply cloud-enabled backup mode.'
            : 'Standalone SD mode selected. Save settings to keep backup and assets fully local.';
        });
        assetModeEl.addEventListener('change', () => {
          const enabled = setCloudOnlineVisibility(backupModeEl.value, assetModeEl.value);
          cloudCaption.textContent = enabled
            ? 'Google sync options are visible. Save settings to apply cloud-enabled asset mode.'
            : 'Standalone SD mode selected. Save settings to keep backup and assets fully local.';
        });
      } else if (isOrdersPage) {
        ordersAddBtn.addEventListener('click', async () => {
          if (state.orders.length >= MAX_ORDER_COUNT) {
            setStatus(`Order limit reached (${MAX_ORDER_COUNT}).`, 'error');
            return;
          }

          try {
            const orderNumber = nextOrderNumber();
            const timestamp = new Date().toISOString();
            const order = {
              order_number: orderNumber,
              created_at: timestamp,
              updated_at: timestamp,
              lines: [createOrderLine()],
            };
            state.orders.unshift(order);
            await saveOrders();
            openOrder(orderNumber);
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        ordersRefreshBtn.addEventListener('click', async () => {
          try {
            state.orders = await loadOrders();
            await refreshItems();
            setStatus('Orders refreshed.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
      } else if (isOrderDetailPage) {
        orderBackBtn.addEventListener('click', () => {
          window.location.href = '/orders';
        });
        orderFulfillBtn.addEventListener('click', () => {
          const order = currentOrder();
          if (!order) {
            setStatus('Order not found.', 'error');
            return;
          }
          openOrderFulfillment(order.order_number);
        });
        orderAddItemBtn.addEventListener('click', async () => {
          const order = currentOrder();
          if (!order) {
            setStatus('Order not found.', 'error');
            return;
          }

          if (order.lines.length >= MAX_ORDER_LINE_COUNT) {
            setStatus(`Line limit reached (${MAX_ORDER_LINE_COUNT}).`, 'error');
            return;
          }

          try {
            order.lines.push(createOrderLine());
            order.updated_at = new Date().toISOString();
            await saveOrders();
            renderOrderDetail();
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        orderRefreshBtn.addEventListener('click', async () => {
          try {
            state.orders = await loadOrders();
            await refreshItems();
            setStatus('Order stock refreshed.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        orderDetailBody.addEventListener('change', async (event) => {
          const select = event.target.closest('[data-order-item]');
          if (!select) {
            return;
          }

          const row = select.closest('tr[data-order-line-id]');
          if (!row) {
            return;
          }

          const order = currentOrder();
          if (!order) {
            return;
          }

          const line = order.lines.find((entry) => entry.lineId === row.dataset.orderLineId);
          if (!line) {
            return;
          }

          try {
            line.itemId = String(select.value || '').trim();
            order.updated_at = new Date().toISOString();
            await saveOrders();
            renderOrderDetail();
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        orderDetailBody.addEventListener('change', async (event) => {
          const qtyInput = event.target.closest('[data-order-needed]');
          if (!qtyInput) {
            return;
          }

          const row = qtyInput.closest('tr[data-order-line-id]');
          if (!row) {
            return;
          }

          const order = currentOrder();
          if (!order) {
            return;
          }

          const line = order.lines.find((entry) => entry.lineId === row.dataset.orderLineId);
          if (!line) {
            return;
          }

          try {
            line.needed = toWholeNumber(qtyInput.value, 0);
            order.updated_at = new Date().toISOString();
            await saveOrders();
            renderOrderDetail();
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
        orderDetailBody.addEventListener('click', async (event) => {
          const removeBtn = event.target.closest('[data-order-remove]');
          if (!removeBtn) {
            return;
          }

          const row = removeBtn.closest('tr[data-order-line-id]');
          if (!row) {
            return;
          }

          const order = currentOrder();
          if (!order) {
            return;
          }

          try {
            order.lines = order.lines.filter((entry) => entry.lineId !== row.dataset.orderLineId);
            ensureOrderLineHeadroom(order);
            order.updated_at = new Date().toISOString();
            await saveOrders();
            renderOrderDetail();
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
      } else if (isOrderFulfillPage) {
        orderFulfillBackBtn.addEventListener('click', () => {
          const order = currentOrder();
          if (!order) {
            window.location.href = '/orders';
            return;
          }
          window.location.href = `/orders/view?order=${encodeURIComponent(order.order_number)}`;
        });
        orderFulfillConfirmBtn.addEventListener('click', async () => {
          const order = currentOrder();
          if (!order) {
            setStatus('Order not found.', 'error');
            return;
          }

          const requirements = orderFulfillmentRequirements(order);
          if (!requirements.length) {
            setStatus('Select at least one item on the order before fulfilling.', 'error');
            return;
          }

          const hasShortage = requirements.some((entry) => entry.shortage > 0);
          if (hasShortage) {
            setStatus('Cannot fulfill while stock is short.', 'error');
            return;
          }

          if (!confirm(`Fulfill ${order.order_number}? Are you sure these parts should be removed from inventory?`)) {
            return;
          }

          const nextOrders = state.orders.filter((entry) => normalizeOrderNumber(entry.order_number) !== normalizeOrderNumber(order.order_number));
          const params = new URLSearchParams();
          params.set('order_number', order.order_number);
          params.set('plan', orderFulfillmentPlan(requirements));
          params.set('orders_payload', JSON.stringify({ orders: nextOrders }));

          try {
            await readJson('/api/orders/fulfill', {
              method: 'POST',
              body: params,
            });
            state.orders = nextOrders;
            setStatus(`${order.order_number} fulfilled and removed from inventory.`, 'ok');
            window.location.href = '/orders';
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });
      } else {
        ordersTabBtn.addEventListener('click', () => {
          window.location.href = '/orders';
        });
        addRowBtn.addEventListener('click', openDraftRow);

        document.getElementById('remove-form').addEventListener('submit', async (event) => {
          try {
            await removeItem(event);
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });

        document.getElementById('search-input').addEventListener('input', (event) => {
          state.search = event.target.value || '';
          renderItems(filteredItems());
        });

        document.querySelectorAll('[data-category]').forEach((button) => {
          button.addEventListener('click', () => {
            setCategory(button.dataset.category);
          });
        });

        document.getElementById('refresh-btn').addEventListener('click', async () => {
          try {
            await refreshItems();
            setStatus('Inventory refreshed.', 'ok');
          } catch (error) {
            setStatus(error.message, 'error');
          }
        });

        document.querySelectorAll('[data-export]').forEach((button) => {
          button.addEventListener('click', () => {
            const category = button.dataset.export || 'all';
            window.location.href = `/api/export?category=${encodeURIComponent(category)}`;
          });
        });
      }

      itemsBody.addEventListener('click', (event) => {
        const imageTrigger = event.target.closest('.image-trigger');
        if (imageTrigger) {
          openImagePreview(
            imageTrigger.dataset.itemId || '',
            imageTrigger.dataset.imageSrc || '',
            imageTrigger.dataset.imageRef || ''
          );
          return;
        }

        const trigger = event.target.closest('.qr-trigger');
        if (!trigger) {
          const actionButton = event.target.closest('[data-draft-action]');
          if (!actionButton || !state.draftItem) {
            return;
          }

          const action = actionButton.dataset.draftAction || '';
          if (action === 'cancel') {
            closeDraftRow();
            return;
          }

          if (action === 'save') {
            (async () => {
              try {
                await saveDraftItem();
              } catch (error) {
                setStatus(error.message, 'error');
              }
            })();
          }
          return;
        }

        openQrPreview(
          trigger.dataset.itemId || '',
          trigger.dataset.qrLink || '',
          trigger.dataset.qrSrc || ''
        );
      });

      itemsBody.addEventListener('input', (event) => {
        if (!state.draftItem) {
          return;
        }

        const field = event.target.dataset.draftField;
        if (field) {
          state.draftItem[field] = event.target.value;
          return;
        }

        const customField = event.target.dataset.draftCustom;
        if (customField) {
          state.draftItem[customField] = event.target.value;
        }
      });

      itemsBody.addEventListener('change', (event) => {
        if (!state.draftItem) {
          return;
        }

        if (event.target.hasAttribute('data-draft-image-file')) {
          state.draftImageFile = event.target.files && event.target.files[0] ? event.target.files[0] : null;
          if (state.draftImageFile) {
            state.draftItem.image_ref = '';
          }
          renderItems(filteredItems());
          return;
        }

        if (event.target.hasAttribute('data-draft-category')) {
          state.draftItem.category = event.target.value || 'part';
          if (normalize(state.draftItem.category) !== 'part') {
            state.draftItem.bom_product = '';
            state.draftItem.bom_qty = '0';
          }
          renderItems(filteredItems());
          return;
        }

        const recentField = event.target.dataset.draftRecent;
        if (!recentField) {
          return;
        }

        if (event.target.value === CUSTOM_OPTION_VALUE) {
          state.draftItem[`${recentField}_mode`] = 'custom';
          renderItems(filteredItems());
          focusDraftField(`[data-draft-custom="${recentField}"]`);
          return;
        }

        state.draftItem[`${recentField}_mode`] = 'select';
        state.draftItem[recentField] = event.target.value || '';
        renderItems(filteredItems());
      });

      itemsBody.addEventListener('keydown', (event) => {
        if (!state.draftItem) {
          return;
        }

        if (event.key === 'Enter' && event.target.closest('.draft-row')) {
          event.preventDefault();
          (async () => {
            try {
              await saveDraftItem();
            } catch (error) {
              setStatus(error.message, 'error');
            }
          })();
        }
      });

      qrModal.addEventListener('click', (event) => {
        if (event.target.hasAttribute('data-close-qr-modal')) {
          closeQrPreview();
        }
      });
      imageModal.addEventListener('click', (event) => {
        if (event.target.hasAttribute('data-close-image-modal')) {
          closeImagePreview();
        }
      });

      qrModalClose.addEventListener('click', closeQrPreview);
      qrModalPrint.addEventListener('click', printQrPreview);
      imageModalClose.addEventListener('click', closeImagePreview);

      document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
          closeQrPreview();
          closeImagePreview();
        }
      });
    }

    boot();
  </script>
</body>
</html>
)HTML";

const char ITEM_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Item Update</title>
  <style>
    :root {
      --bg: #ebf0f3;
      --panel: #ffffff;
      --line: #d2dbe4;
      --ink: #1e2f40;
      --muted: #607388;
      --accent: #127f78;
      --accent-dark: #0d615b;
      --positive: #1f985b;
      --positive-dark: #157545;
      --danger: #be2f2f;
      --danger-dark: #8d2323;
      --zero: #8d2323;
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      background:
        radial-gradient(circle at top left, #f9fcfe 0, #edf4f7 30%, #e4ecf1 100%);
      color: var(--ink);
      font-family: "Segoe UI", Tahoma, sans-serif;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 1.2rem;
    }

    .card {
      width: min(1180px, 100%);
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 22px;
      padding: 1.35rem;
      box-shadow: 0 18px 40px rgba(20, 40, 60, 0.12);
    }

    .page-header {
      display: flex;
      align-items: center;
      gap: 0.9rem;
      margin-bottom: 1rem;
    }

    .page-brand-logo {
      width: 64px;
      height: 64px;
      object-fit: contain;
      border-radius: 16px;
      border: 1px solid var(--line);
      background: #ffffff;
      padding: 0.45rem;
    }

    h1 {
      margin: 0;
      font-size: clamp(1.7rem, 3vw, 2.3rem);
    }

    .page-header p {
      margin: 0.35rem 0 0;
      color: var(--muted);
    }

    .back-link {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-width: 165px;
      padding: 0.8rem 1rem;
      border: 1px solid var(--line);
      border-radius: 12px;
      background: #f5f8fb;
      color: var(--ink);
      font-weight: 600;
      text-decoration: none;
    }

    .page-footer {
      display: flex;
      justify-content: center;
      margin-top: 1rem;
    }

    .top-grid {
      display: grid;
      grid-template-columns: minmax(240px, 0.8fr) minmax(0, 1.45fr);
      gap: 1rem;
      margin-bottom: 1rem;
      align-items: stretch;
    }

    .details-grid {
      display: grid;
      grid-template-columns: minmax(0, 1.8fr) minmax(220px, 0.75fr);
      gap: 1rem;
      margin-bottom: 1rem;
    }

    .info-panel,
    .media-box,
    .quantity-shell,
    .manual-panel,
    .status {
      border: 1px solid var(--line);
      border-radius: 18px;
      background: #f9fbfc;
    }

    .info-panel {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 0.8rem;
      padding: 1rem;
      background: linear-gradient(145deg, #fbfeff, #f2f7fb);
    }

    .info-box {
      padding: 0.9rem 1rem;
      border-radius: 14px;
      background: rgba(255, 255, 255, 0.9);
      border: 1px solid #e1e8ee;
    }

    .info-label,
    .media-label,
    .quantity-label,
    .field-label {
      display: block;
      margin-bottom: 0.35rem;
      color: var(--muted);
      font-size: 0.86rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      font-weight: 700;
    }

    .info-value {
      font-size: 1.2rem;
      font-weight: 700;
      word-break: break-word;
    }

    .info-value.compact {
      font-size: 1rem;
      font-weight: 600;
    }

    .media-box {
      padding: 0.95rem;
      display: flex;
      flex-direction: column;
      gap: 0.7rem;
      min-height: 100%;
    }

    .media-box a {
      text-decoration: none;
    }

    .qr-frame,
    .photo-placeholder {
      flex: 1;
      min-height: 210px;
      border-radius: 16px;
      background: #ffffff;
      border: 1px solid #dde6ec;
      display: flex;
      align-items: center;
      justify-content: center;
      overflow: hidden;
    }

    .qr-frame {
      padding: 0.9rem;
    }

    .qr-image {
      display: block;
      width: 100%;
      max-width: 180px;
      height: auto;
      image-rendering: pixelated;
    }

    .photo-placeholder {
      padding: 1rem;
      flex-direction: column;
      gap: 0.7rem;
      text-align: center;
      background:
        linear-gradient(145deg, #f8fbfd, #eef4f8);
    }

    .photo-placeholder.has-image {
      padding: 0;
      background: #ffffff;
    }

    .photo-image {
      display: block;
      width: 100%;
      height: 100%;
      object-fit: contain;
      background: #ffffff;
    }

    .photo-empty {
      display: contents;
    }

    .photo-initials {
      width: 96px;
      height: 96px;
      border-radius: 24px;
      display: grid;
      place-items: center;
      background: linear-gradient(145deg, #dceaf2, #c7dbe8);
      color: #234158;
      font-size: 2rem;
      font-weight: 800;
      letter-spacing: 0.08em;
    }

    .photo-name {
      font-size: 1rem;
      font-weight: 700;
    }

    .photo-note {
      color: var(--muted);
      font-size: 0.92rem;
    }

    .quantity-shell {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 150px;
      gap: 1rem;
      padding: 1rem;
      background: linear-gradient(145deg, #f9fcfd, #eff5f8);
    }

    .quantity-card {
      min-height: 240px;
      padding: 1rem 1.1rem;
      border-radius: 18px;
      background: #ffffff;
      border: 1px solid #dee7ed;
      display: flex;
      flex-direction: column;
      justify-content: center;
    }

    .quantity-meta {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 0.8rem;
      margin-top: 1rem;
    }

    .quantity-stat {
      padding: 0.8rem 0.9rem;
      border-radius: 14px;
      background: #f4f8fb;
      border: 1px solid #dbe5ec;
    }

    .quantity-stat-label {
      display: block;
      color: var(--muted);
      font-size: 0.78rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      font-weight: 700;
      margin-bottom: 0.25rem;
    }

    .quantity-stat-value {
      font-size: 1.5rem;
      font-weight: 800;
      line-height: 1.05;
    }

    .quantity-stat-value.positive {
      color: var(--positive-dark);
    }

    .quantity-stat-value.negative {
      color: var(--danger-dark);
    }

    .quantity-stat-value.neutral {
      color: var(--ink);
    }

    .quantity-value {
      margin-top: 0.5rem;
      font-size: clamp(4rem, 14vw, 6.5rem);
      line-height: 0.95;
      font-weight: 800;
      letter-spacing: -0.04em;
    }

    .quantity-value.zero {
      color: var(--zero);
    }

    .quick-buttons {
      display: grid;
      grid-template-rows: 1fr 1fr;
      gap: 1rem;
    }

    input, button {
      font: inherit;
      border-radius: 14px;
    }

    input {
      width: 100%;
      border: 1px solid var(--line);
      padding: 0.9rem 1rem;
      background: #ffffff;
      color: var(--ink);
      font-size: 1rem;
    }

    button {
      cursor: pointer;
      font-weight: 700;
      transition: transform 0.12s ease, filter 0.12s ease;
    }

    button:hover {
      filter: brightness(1.03);
    }

    button:active {
      transform: translateY(1px);
    }

    .quick-button {
      min-height: 112px;
      border: none;
      color: #fff;
      font-size: 3.5rem;
      line-height: 1;
      box-shadow: inset 0 -3px 0 rgba(0, 0, 0, 0.12);
    }

    .quick-button.add {
      background: linear-gradient(180deg, var(--positive), var(--positive-dark));
    }

    .quick-button.subtract {
      background: linear-gradient(180deg, var(--danger), var(--danger-dark));
    }

    .manual-panel {
      padding: 1rem;
      background: linear-gradient(145deg, #fbfdfe, #f1f6f9);
    }

    .bom-panel {
      margin-top: 1rem;
      padding: 1rem;
      border: 1px solid var(--line);
      border-radius: 18px;
      background: linear-gradient(145deg, #fcfefe, #f1f5f8);
    }

    .bom-panel h2 {
      margin: 0 0 0.35rem;
      font-size: 1.05rem;
    }

    .bom-panel p {
      margin: 0 0 0.85rem;
      color: var(--muted);
    }

    .bom-list {
      display: grid;
      gap: 0.7rem;
    }

    .bom-item {
      display: grid;
      grid-template-columns: minmax(0, 1.5fr) repeat(4, minmax(0, 1fr));
      gap: 0.7rem;
      padding: 0.8rem 0.9rem;
      border-radius: 14px;
      border: 1px solid #dde6ec;
      background: #ffffff;
      align-items: center;
    }

    .bom-name {
      font-weight: 700;
    }

    .bom-meta {
      color: var(--muted);
      font-size: 0.9rem;
      word-break: break-word;
    }

    .bom-empty {
      padding: 0.9rem 1rem;
      border: 1px dashed var(--line);
      border-radius: 14px;
      color: var(--muted);
      background: #ffffff;
    }

    .manual-panel h2 {
      margin: 0 0 0.9rem;
      font-size: 1.05rem;
    }

    .manual-grid {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 170px 170px;
      gap: 0.8rem;
      align-items: end;
    }

    .manual-grid + .manual-grid {
      margin-top: 0.8rem;
    }

    .field {
      display: block;
    }

    .field-wide {
      grid-column: span 2;
    }

    .manual-button {
      min-height: 58px;
      border: none;
      color: #fff;
      padding: 0.9rem 1rem;
    }

    .manual-button.add {
      background: linear-gradient(180deg, var(--positive), var(--positive-dark));
    }

    .manual-button.subtract {
      background: linear-gradient(180deg, var(--danger), var(--danger-dark));
    }

    .manual-button.secondary {
      background: linear-gradient(180deg, var(--accent), var(--accent-dark));
    }

    .status {
      margin-top: 1rem;
      padding: 0.9rem 1rem;
      background: #ffffff;
    }

    .status.ok {
      color: var(--accent-dark);
      border-color: #c5ddd8;
      background: #f3fcf9;
    }

    .status.error {
      color: var(--danger-dark);
      border-color: #ebc8c8;
      background: #fff4f4;
    }

    a { color: #0a5a97; }

    @media (max-width: 920px) {
      .top-grid,
      .details-grid,
      .quantity-shell,
      .manual-grid {
        grid-template-columns: 1fr;
      }

      .info-panel {
        grid-template-columns: 1fr;
      }

      .field-wide {
        grid-column: span 1;
      }

      .quick-buttons {
        grid-template-columns: 1fr 1fr;
        grid-template-rows: 1fr;
      }

      .quick-button {
        min-height: 84px;
      }

      .quantity-meta {
        grid-template-columns: 1fr;
      }

      .bom-item {
        grid-template-columns: 1fr;
      }
    }

    @media (max-width: 640px) {
      body {
        padding: 0.8rem;
      }

      .card {
        padding: 1rem;
        border-radius: 18px;
      }

      .page-header {
        margin-bottom: 0.8rem;
      }

      .quantity-card {
        min-height: 190px;
      }

      .qr-frame,
      .photo-placeholder {
        min-height: 170px;
      }
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="page-header">
      <img id="page-brand-logo" class="page-brand-logo" alt="Brand logo" hidden>
      <div>
        <h1 id="page-brand-title">Inventory Update</h1>
        <p>Scan the QR, verify the part or product details, and adjust stock directly on the device.</p>
      </div>
    </div>

    <div class="top-grid">
      <section class="media-box">
        <span class="media-label">Image Reference</span>
        <div class="photo-placeholder" id="item-photo-shell">
          <img id="item-photo-image" class="photo-image" alt="Item reference image" hidden>
          <div id="item-photo-empty" class="photo-empty">
            <div class="photo-initials" id="item-photo-initials">--</div>
            <div class="photo-name" id="item-photo-name">No item selected</div>
            <div class="photo-note" id="item-photo-note">No image reference stored.</div>
          </div>
        </div>
      </section>

      <div class="quantity-shell">
        <div class="quantity-card">
          <span class="quantity-label">Inventory Quantity</span>
          <div class="quantity-value" id="item-qty">-</div>
          <div class="quantity-meta">
            <div class="quantity-stat">
              <span class="quantity-stat-label">Starting Quantity</span>
              <div class="quantity-stat-value neutral" id="item-start-qty">-</div>
            </div>
            <div class="quantity-stat">
              <span class="quantity-stat-label">Change This Visit</span>
              <div class="quantity-stat-value neutral" id="item-session-delta">0</div>
            </div>
          </div>
        </div>

        <div class="quick-buttons">
          <button id="plus-one" class="quick-button add" type="button" aria-label="Add one">+</button>
          <button id="minus-one" class="quick-button subtract" type="button" aria-label="Subtract one">-</button>
        </div>
      </div>
    </div>

    <div class="details-grid">
      <section class="info-panel">
        <div class="info-box">
          <span class="info-label">Part Number</span>
          <div class="info-value" id="item-id">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">Category</span>
          <div class="info-value compact" id="item-category">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">Part Name</span>
          <div class="info-value" id="item-name">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">QR Code</span>
          <div class="info-value compact" id="item-qr-code">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">Color</span>
          <div class="info-value compact" id="item-color">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">Material</span>
          <div class="info-value compact" id="item-material">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">BOM Product / Kit</span>
          <div class="info-value compact" id="item-bom-product">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">BOM Qty</span>
          <div class="info-value compact" id="item-bom-qty">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">Quantity</span>
          <div class="info-value" id="item-summary-qty">-</div>
        </div>
        <div class="info-box">
          <span class="info-label">Updated</span>
          <div class="info-value compact" id="item-updated">-</div>
        </div>
      </section>

      <section class="media-box">
        <span class="media-label">QR Code</span>
        <a id="qr-link" href="#" target="_blank" rel="noopener">
          <div class="qr-frame">
            <img id="qr-image" class="qr-image" alt="QR code for this item">
          </div>
        </a>
      </section>
    </div>

    <section class="manual-panel">
      <h2>Manual Entry</h2>
      <div class="manual-grid">
        <label class="field">
          <span class="field-label">Adjust by amount</span>
          <input id="change-qty" type="number" value="1" min="1" placeholder="Enter amount">
        </label>
        <button id="add-btn" class="manual-button add" type="button">Add</button>
        <button id="sub-btn" class="manual-button subtract" type="button">Subtract</button>
      </div>

      <div class="manual-grid">
        <label class="field field-wide">
          <span class="field-label">Set exact quantity</span>
          <input id="set-qty" type="number" min="0" placeholder="Set exact quantity">
        </label>
        <button id="set-btn" class="manual-button secondary" type="button">Set Quantity</button>
      </div>
    </section>

    <section class="bom-panel">
      <h2>BOM Components</h2>
      <p id="bom-caption">Parts assigned to this product or kit appear here.</p>
      <div id="bom-list" class="bom-list">
        <div class="bom-empty">No BOM components loaded.</div>
      </div>
    </section>

    <div id="status" class="status ok">Ready.</div>
    <div class="page-footer">
      <a class="back-link" href="/">Back to inventory</a>
    </div>
  </div>

  <script>
    const params = new URLSearchParams(window.location.search);
    const itemId = params.get('id');
    let sessionStartQty = null;
    let currentItem = null;
    let pendingAdjustDelta = 0;
    let adjustFlushTimer = null;
    let adjustRequestInFlight = false;
    let adjustInflightPromise = null;
    const ADJUST_FLUSH_DELAY_MS = 550;

    const pageBrandLogoEl = document.getElementById('page-brand-logo');
    const pageBrandTitleEl = document.getElementById('page-brand-title');
    const idEl = document.getElementById('item-id');
    const categoryEl = document.getElementById('item-category');
    const nameEl = document.getElementById('item-name');
    const qrCodeEl = document.getElementById('item-qr-code');
    const colorEl = document.getElementById('item-color');
    const materialEl = document.getElementById('item-material');
    const bomProductEl = document.getElementById('item-bom-product');
    const bomQtyEl = document.getElementById('item-bom-qty');
    const qtyEl = document.getElementById('item-qty');
    const startQtyEl = document.getElementById('item-start-qty');
    const sessionDeltaEl = document.getElementById('item-session-delta');
    const summaryQtyEl = document.getElementById('item-summary-qty');
    const updatedEl = document.getElementById('item-updated');
    const changeQtyEl = document.getElementById('change-qty');
    const setQtyEl = document.getElementById('set-qty');
    const plusOneBtn = document.getElementById('plus-one');
    const minusOneBtn = document.getElementById('minus-one');
    const addBtn = document.getElementById('add-btn');
    const subBtn = document.getElementById('sub-btn');
    const setBtn = document.getElementById('set-btn');
    const qrImageEl = document.getElementById('qr-image');
    const qrLinkEl = document.getElementById('qr-link');
    const photoShellEl = document.getElementById('item-photo-shell');
    const photoImageEl = document.getElementById('item-photo-image');
    const photoEmptyEl = document.getElementById('item-photo-empty');
    const photoInitialsEl = document.getElementById('item-photo-initials');
    const photoNameEl = document.getElementById('item-photo-name');
    const photoNoteEl = document.getElementById('item-photo-note');
    const bomCaptionEl = document.getElementById('bom-caption');
    const bomListEl = document.getElementById('bom-list');
    const statusEl = document.getElementById('status');

    function setStatus(message, isError = false) {
      statusEl.textContent = message;
      statusEl.className = isError ? 'status error' : 'status ok';
    }

    function initialsFromName(name) {
      const parts = String(name).trim().split(/\s+/).filter(Boolean);
      if (!parts.length) {
        return '--';
      }

      return parts
        .slice(0, 2)
        .map((part) => part.charAt(0).toUpperCase())
        .join('');
    }

    function displayValue(value) {
      return value ? String(value) : '-';
    }

    function formatDelta(delta) {
      return delta > 0 ? `+${delta}` : String(delta);
    }

    function applyBranding(status) {
      const brandName = status && status.brand_name ? status.brand_name : 'Inventory Update';
      const brandLogoRef = status && status.brand_logo_ref ? status.brand_logo_ref : '';
      pageBrandTitleEl.textContent = `${brandName} Inventory Update`;
      document.title = `${brandName} Item Update`;

      const logoUrl = resolveImageUrl(brandLogoRef);
      if (logoUrl) {
        pageBrandLogoEl.hidden = false;
        pageBrandLogoEl.src = logoUrl;
        pageBrandLogoEl.alt = `${brandName} logo`;
      } else {
        pageBrandLogoEl.hidden = true;
        pageBrandLogoEl.removeAttribute('src');
      }
    }

    function resolveImageUrl(imageRef) {
      const value = String(imageRef || '').trim();
      if (!value) {
        return '';
      }

      if (value.startsWith('/') || value.startsWith('http://') || value.startsWith('https://')) {
        return value;
      }

      return '';
    }

    function renderBomComponents(item, components) {
      bomListEl.innerHTML = '';

      if (!(item.has_bom || item.category === 'product' || item.category === 'kit')) {
        bomCaptionEl.textContent = 'This item is not a product or kit, so it does not own a BOM list.';
        bomListEl.innerHTML = '<div class="bom-empty">Parts can still point to this item through the BOM Product / Kit field.</div>';
        return;
      }

      bomCaptionEl.textContent = `Parts assigned to ${item.part_name || 'this item'} are treated as its BOM.`;

      if (!components.length) {
        bomListEl.innerHTML = '<div class="bom-empty">No parts currently point at this product or kit.</div>';
        return;
      }

      for (const component of components) {
        const node = document.createElement('div');
        node.className = 'bom-item';
        node.innerHTML = `
          <div>
            <div class="bom-name">${displayValue(component.part_name)}</div>
            <div class="bom-meta">Part Number: ${displayValue(component.id)}</div>
          </div>
          <div class="bom-meta">Color: ${displayValue(component.color)}</div>
          <div class="bom-meta">Material: ${displayValue(component.material)}</div>
          <div class="bom-meta">BOM Qty: ${component.bom_qty || 0}</div>
          <div class="bom-meta">Stock: ${component.qty}</div>
        `;
        bomListEl.appendChild(node);
      }
    }

    function setSessionDelta(currentQty) {
      if (sessionStartQty === null) {
        sessionStartQty = currentQty;
      }

      const delta = currentQty - sessionStartQty;
      startQtyEl.textContent = sessionStartQty;
      sessionDeltaEl.textContent = delta > 0 ? `+${delta}` : String(delta);
      sessionDeltaEl.className = `quantity-stat-value ${delta > 0 ? 'positive' : delta < 0 ? 'negative' : 'neutral'}`;
    }

    async function readJson(url, options = {}) {
      const response = await fetch(url, options);
      const data = await response.json().catch(() => ({}));
      if (!response.ok) {
        throw new Error(data.error || `Request failed (${response.status})`);
      }
      return data;
    }

    async function loadStatus() {
      const data = await readJson('/api/status');
      applyBranding(data);
    }

    function updateMeta(item) {
      currentItem = { ...item };
      const qty = Number(currentItem.qty);

      idEl.textContent = currentItem.id;
      categoryEl.textContent = displayValue(currentItem.category_label || currentItem.category);
      nameEl.textContent = displayValue(currentItem.part_name);
      qrCodeEl.textContent = displayValue(currentItem.qr_code);
      colorEl.textContent = displayValue(currentItem.color);
      materialEl.textContent = displayValue(currentItem.material);
      bomProductEl.textContent = displayValue(currentItem.bom_product);
      bomQtyEl.textContent = displayValue(currentItem.bom_qty);
      qtyEl.textContent = qty;
      qtyEl.className = `quantity-value ${qty === 0 ? 'zero' : ''}`.trim();
      summaryQtyEl.textContent = qty;
      updatedEl.textContent = currentItem.updated_at;
      if (document.activeElement !== setQtyEl) {
        setQtyEl.value = qty;
      }
      setSessionDelta(qty);

      const itemUrl = currentItem.qr_link || `${window.location.origin}/item?id=${encodeURIComponent(currentItem.id)}`;
      const qrSrc = `/qr.svg?data=${encodeURIComponent(itemUrl)}`;
      qrImageEl.src = qrSrc;
      qrImageEl.alt = `QR code for item ${currentItem.id}`;
      qrLinkEl.href = qrSrc;

      const displayName = currentItem.part_name || '';
      photoInitialsEl.textContent = initialsFromName(displayName);
      photoNameEl.textContent = displayName || 'Unnamed item';
      photoNoteEl.textContent = currentItem.image_ref ? `Image Ref: ${currentItem.image_ref}` : 'No image reference stored.';

      const imageUrl = resolveImageUrl(currentItem.image_ref);
      if (imageUrl) {
        photoShellEl.classList.add('has-image');
        photoImageEl.hidden = false;
        photoEmptyEl.hidden = true;
        photoImageEl.src = imageUrl;
        photoImageEl.alt = displayName ? `${displayName} reference image` : 'Item reference image';
      } else {
        photoShellEl.classList.remove('has-image');
        photoImageEl.hidden = true;
        photoImageEl.removeAttribute('src');
        photoEmptyEl.hidden = false;
      }
    }

    photoImageEl.addEventListener('error', () => {
      photoShellEl.classList.remove('has-image');
      photoImageEl.hidden = true;
      photoImageEl.removeAttribute('src');
      photoEmptyEl.hidden = false;
      if (photoNoteEl.textContent && !photoNoteEl.textContent.includes('could not be loaded')) {
        photoNoteEl.textContent += ' (file could not be loaded)';
      }
    });

    async function loadItem() {
      if (!itemId) {
        throw new Error('Missing item id in URL.');
      }
      const data = await readJson(`/api/item?id=${encodeURIComponent(itemId)}`);
      updateMeta(data.item);
      renderBomComponents(data.item, data.bom_components || []);
    }

    function scheduleAdjustFlush(delay = ADJUST_FLUSH_DELAY_MS) {
      if (adjustFlushTimer) {
        window.clearTimeout(adjustFlushTimer);
      }

      adjustFlushTimer = window.setTimeout(() => {
        flushPendingAdjust().catch((error) => {
          setStatus(error.message, true);
        });
      }, delay);
    }

    function queueAdjust(delta) {
      if (!currentItem) {
        throw new Error('Item is still loading.');
      }

      const nextQty = Number(currentItem.qty) + delta;
      if (nextQty < 0) {
        throw new Error('Quantity cannot go below zero.');
      }

      updateMeta({
        ...currentItem,
        qty: nextQty,
      });

      pendingAdjustDelta += delta;
      scheduleAdjustFlush();
      setStatus(`Queued ${formatDelta(pendingAdjustDelta)}. Saving shortly...`);
    }

    async function flushPendingAdjust() {
      if (adjustFlushTimer) {
        window.clearTimeout(adjustFlushTimer);
        adjustFlushTimer = null;
      }

      if (adjustRequestInFlight) {
        return adjustInflightPromise || Promise.resolve();
      }

      if (!pendingAdjustDelta) {
        return Promise.resolve();
      }

      const deltaToSend = pendingAdjustDelta;
      pendingAdjustDelta = 0;
      adjustRequestInFlight = true;

      adjustInflightPromise = (async () => {
        const body = new URLSearchParams();
        body.set('id', itemId);
        body.set('delta', String(deltaToSend));

        try {
          const data = await readJson('/api/items/adjust', {
            method: 'POST',
            body,
          });

          if (pendingAdjustDelta) {
            updateMeta({
              ...data.item,
              qty: Number(data.item.qty) + pendingAdjustDelta,
            });
          } else {
            updateMeta(data.item);
          }
          renderBomComponents(data.item, data.bom_components || []);

          if (pendingAdjustDelta) {
            setStatus(`Saved ${formatDelta(deltaToSend)}. Pending ${formatDelta(pendingAdjustDelta)}...`);
            scheduleAdjustFlush();
          } else {
            setStatus(`Inventory changed by ${formatDelta(deltaToSend)}.`);
          }
        } catch (error) {
          if (currentItem) {
            updateMeta({
              ...currentItem,
              qty: Number(currentItem.qty) - deltaToSend,
            });
          }
          pendingAdjustDelta += deltaToSend;
          throw error;
        } finally {
          adjustRequestInFlight = false;
          adjustInflightPromise = null;
        }
      })();

      return adjustInflightPromise;
    }

    async function adjust(delta) {
      queueAdjust(delta);
      await flushPendingAdjust();
    }

    async function setQty() {
      await flushPendingAdjust();

      const qty = Number(setQtyEl.value);
      if (!Number.isInteger(qty) || qty < 0) {
        throw new Error('Quantity must be a whole number >= 0.');
      }
      const body = new URLSearchParams();
      body.set('id', itemId);
      body.set('qty', String(qty));
      const data = await readJson('/api/items/set', {
        method: 'POST',
        body,
      });
      updateMeta(data.item);
      renderBomComponents(data.item, data.bom_components || []);
    }

    async function boot() {
      try {
        await loadStatus();
        await loadItem();
        setStatus('Item loaded.');
      } catch (error) {
        setStatus(error.message, true);
      }

      plusOneBtn.addEventListener('click', () => {
        try {
          queueAdjust(1);
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      minusOneBtn.addEventListener('click', () => {
        try {
          queueAdjust(-1);
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      addBtn.addEventListener('click', async () => {
        try {
          const amount = Number(changeQtyEl.value);
          if (!Number.isInteger(amount) || amount <= 0) {
            throw new Error('Enter a positive whole number.');
          }
          await adjust(amount);
          setStatus(`Added ${amount}.`);
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      subBtn.addEventListener('click', async () => {
        try {
          const amount = Number(changeQtyEl.value);
          if (!Number.isInteger(amount) || amount <= 0) {
            throw new Error('Enter a positive whole number.');
          }
          await adjust(-amount);
          setStatus(`Subtracted ${amount}.`);
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      setBtn.addEventListener('click', async () => {
        try {
          await setQty();
          setStatus('Quantity updated.');
        } catch (error) {
          setStatus(error.message, true);
        }
      });

      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'hidden' && pendingAdjustDelta && !adjustRequestInFlight) {
          flushPendingAdjust().catch(() => {
          });
        }
      });
    }

    boot();
  </script>
</body>
</html>
)HTML";

bool writeTextFile(const char* path, const char* content) {
  storageFs().remove(path);
  File f = storageFs().open(path, FILE_WRITE);
  if (!f) {
    return false;
  }

  const size_t length = strlen(content);
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(content), length);
  f.close();
  return written == length;
}

String readTextFileTrimmed(const char* path) {
  File f = storageFs().open(path, FILE_READ);
  if (!f) {
    return "";
  }

  String value = f.readString();
  f.close();
  value.trim();
  return value;
}

bool syncUiAssetsToSd(bool forceRewrite = false) {
  if (!g_sdReady) {
    return false;
  }

  fs::FS& fs = storageFs();
  if (!fs.exists(UI_DIR)) {
    fs.mkdir(UI_DIR);
  }

  const bool needsWrite =
    forceRewrite ||
    !fs.exists(UI_INDEX_FILE) ||
    !fs.exists(UI_ITEM_FILE) ||
    readTextFileTrimmed(UI_VERSION_FILE) != String(UI_ASSET_VERSION);

  if (!needsWrite) {
    return true;
  }

  if (!writeTextFile(UI_INDEX_FILE, INDEX_HTML)) {
    return false;
  }

  if (!writeTextFile(UI_ITEM_FILE, ITEM_HTML)) {
    return false;
  }

  if (!writeTextFile(UI_VERSION_FILE, UI_ASSET_VERSION)) {
    return false;
  }

  markCloudDirty("ui_assets_updated");
  return true;
}

bool streamHtmlFromSd(const char* path) {
  if (!g_sdReady) {
    return false;
  }

  File f = storageFs().open(path, FILE_READ);
  if (!f) {
    return false;
  }

  server.streamFile(f, "text/html; charset=utf-8");
  f.close();
  return true;
}

String sanitizeField(String value) {
  value.trim();
  value.replace("\n", " ");
  value.replace("\r", " ");
  value.replace("|", " ");
  return value;
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '"') {
      out += "\\\"";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }

  return out;
}

String csvEscape(const String& value) {
  String out = value;
  out.replace("\"", "\"\"");
  return "\"" + out + "\"";
}

String currentTimestamp() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return "UPTIME+" + String(millis() / 1000) + "s";
  }

  struct tm timeInfo;
  gmtime_r(&now, &timeInfo);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeInfo);
  return String(buffer);
}

bool isUnreservedUrlChar(char c) {
  return isAlphaNumeric(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~';
}

String urlEncode(const String& value) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3);

  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value.charAt(i));
    if (isUnreservedUrlChar(static_cast<char>(c))) {
      out += static_cast<char>(c);
      continue;
    }

    out += '%';
    out += hex[(c >> 4) & 0x0F];
    out += hex[c & 0x0F];
  }

  return out;
}

String itemUrl(const String& id) {
  return g_baseUrl + "/item?id=" + urlEncode(id);
}

bool isAllowedImageExtension(const String& extension) {
  return extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".gif" || extension == ".bmp" || extension == ".webp";
}

String fileExtension(String filename) {
  const int dot = filename.lastIndexOf('.');
  if (dot < 0) {
    return "";
  }

  String extension = filename.substring(dot);
  extension.toLowerCase();
  return extension;
}

String sanitizeFilenameStem(const String& value) {
  String out;
  out.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (isAlphaNumeric(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
      out += c;
    } else {
      out += '_';
    }
  }

  while (out.indexOf("__") >= 0) {
    out.replace("__", "_");
  }

  out.trim();
  if (out.isEmpty()) {
    return "image";
  }

  return out;
}

bool isSafeStorageImagePath(const String& path) {
  const String prefix = String(IMAGE_DIR) + "/";
  return path.startsWith(prefix) && path.indexOf("..") < 0;
}

String imageRefFromStoragePath(const String& path) {
  return "/api/files?path=" + urlEncode(path);
}

String contentTypeForPath(String path) {
  path.toLowerCase();

  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) {
    return "image/jpeg";
  }
  if (path.endsWith(".png")) {
    return "image/png";
  }
  if (path.endsWith(".gif")) {
    return "image/gif";
  }
  if (path.endsWith(".bmp")) {
    return "image/bmp";
  }
  if (path.endsWith(".webp")) {
    return "image/webp";
  }

  return "application/octet-stream";
}

String uniqueImageStoragePath(const String& originalName) {
  const String extension = fileExtension(originalName);
  if (!isAllowedImageExtension(extension)) {
    return "";
  }

  String stem = originalName;
  const int dot = stem.lastIndexOf('.');
  if (dot >= 0) {
    stem = stem.substring(0, dot);
  }
  stem = sanitizeFilenameStem(stem);

  const String prefix = String(IMAGE_DIR) + "/" + String(static_cast<uint32_t>(millis())) + "_" + stem;
  String candidate = prefix + extension;
  uint16_t counter = 2;
  while (storageFs().exists(candidate)) {
    candidate = prefix + "_" + String(counter) + extension;
    ++counter;
  }

  return candidate;
}

bool buildQrCode(QRCode& qrcode, std::vector<uint8_t>& modules, const String& value) {
  static const uint8_t versions[] = {4, 5, 6, 7, 8};

  for (uint8_t version : versions) {
    modules.assign(qrcode_getBufferSize(version), 0);
    if (qrcode_initText(&qrcode, modules.data(), version, ECC_MEDIUM, value.c_str()) == 0) {
      return true;
    }
  }

  return false;
}

String qrCodeSvg(const String& value) {
  QRCode qrcode;
  std::vector<uint8_t> modules;
  if (!buildQrCode(qrcode, modules, value)) {
    return "";
  }

  const uint8_t quietZone = 4;
  const uint8_t viewBoxSize = qrcode.size + (quietZone * 2);

  String svg;
  svg.reserve(7000);
  svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ";
  svg += String(viewBoxSize);
  svg += " ";
  svg += String(viewBoxSize);
  svg += "\" shape-rendering=\"crispEdges\">";
  svg += "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>";
  svg += "<path d=\"";

  for (uint8_t y = 0; y < qrcode.size; ++y) {
    for (uint8_t x = 0; x < qrcode.size; ++x) {
      if (!qrcode_getModule(&qrcode, x, y)) {
        continue;
      }

      svg += "M";
      svg += String(x + quietZone);
      svg += ",";
      svg += String(y + quietZone);
      svg += "h1v1h-1z";
    }
  }

  svg += "\" fill=\"#111827\"/></svg>";
  return svg;
}

int findItemIndex(const String& id) {
  const String lookupId = normalizeLookupValue(id);
  for (size_t i = 0; i < g_items.size(); ++i) {
    if (normalizeLookupValue(g_items[i].id) == lookupId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool parseIdArg(const char* argName, String& outValue) {
  if (!server.hasArg(argName)) {
    return false;
  }

  outValue = trimCopy(server.arg(argName));
  if (outValue.isEmpty()) {
    return false;
  }
  return true;
}

bool parseIntArg(const char* argName, int32_t& outValue) {
  if (!server.hasArg(argName)) {
    return false;
  }

  String raw = server.arg(argName);
  raw.trim();
  if (raw.isEmpty()) {
    return false;
  }

  size_t start = 0;
  if (raw.charAt(0) == '-' || raw.charAt(0) == '+') {
    if (raw.length() == 1) {
      return false;
    }
    start = 1;
  }

  for (size_t i = start; i < raw.length(); ++i) {
    if (!isDigit(raw.charAt(i))) {
      return false;
    }
  }

  const long parsed = strtol(raw.c_str(), nullptr, 10);
  outValue = static_cast<int32_t>(parsed);
  return true;
}

bool parseIntText(const String& rawValue, int32_t& outValue) {
  String raw = trimCopy(rawValue);
  if (raw.isEmpty()) {
    return false;
  }

  size_t start = 0;
  if (raw.charAt(0) == '-' || raw.charAt(0) == '+') {
    if (raw.length() == 1) {
      return false;
    }
    start = 1;
  }

  for (size_t i = start; i < raw.length(); ++i) {
    if (!isDigit(raw.charAt(i))) {
      return false;
    }
  }

  const long parsed = strtol(raw.c_str(), nullptr, 10);
  outValue = static_cast<int32_t>(parsed);
  return true;
}

bool validateOrdersPayload(const String& payload, String& errorMessage) {
  const String trimmed = trimCopy(payload);
  if (trimmed.isEmpty()) {
    errorMessage = "Orders payload cannot be empty.";
    return false;
  }

  if (trimmed.length() > MAX_ORDERS_PAYLOAD_BYTES) {
    errorMessage = "Orders payload is too large.";
    return false;
  }

  if (!trimmed.startsWith("{") || trimmed.indexOf("\"orders\"") < 0) {
    errorMessage = "Invalid orders payload.";
    return false;
  }

  errorMessage = "";
  return true;
}

bool parseOrderFulfillmentPlan(const String& rawPlan, std::vector<OrderFulfillmentEntry>& outEntries, String& errorMessage) {
  outEntries.clear();
  String plan = rawPlan;
  plan.replace("\r", "\n");
  if (plan.length() > MAX_ORDER_FULFILL_PLAN_BYTES) {
    errorMessage = "Fulfillment plan is too large.";
    return false;
  }

  int start = 0;
  while (start <= static_cast<int>(plan.length())) {
    const int end = plan.indexOf('\n', start);
    const int next = end < 0 ? static_cast<int>(plan.length()) : end;
    String line = plan.substring(start, next);
    line = trimCopy(line);
    start = end < 0 ? static_cast<int>(plan.length()) + 1 : end + 1;

    if (line.isEmpty()) {
      continue;
    }

    const std::vector<String> fields = splitPipeLine(line);
    if (fields.size() < 2) {
      errorMessage = "Invalid fulfillment line format.";
      return false;
    }

    const String itemId = trimCopy(fields[0]);
    if (itemId.isEmpty()) {
      errorMessage = "Fulfillment item id is required.";
      return false;
    }

    int32_t needed = 0;
    if (!parseIntText(fields[1], needed) || needed <= 0) {
      errorMessage = "Fulfillment quantity must be a positive integer.";
      return false;
    }

    const String lookup = normalizeLookupValue(itemId);
    bool merged = false;
    for (size_t i = 0; i < outEntries.size(); ++i) {
      if (normalizeLookupValue(outEntries[i].itemId) == lookup) {
        outEntries[i].needed += needed;
        merged = true;
        break;
      }
    }

    if (!merged) {
      OrderFulfillmentEntry entry;
      entry.itemId = itemId;
      entry.needed = needed;
      outEntries.push_back(entry);
    }

    if (outEntries.size() > MAX_ORDER_FULFILL_LINES) {
      errorMessage = "Fulfillment plan has too many lines.";
      return false;
    }
  }

  if (outEntries.empty()) {
    errorMessage = "No selected items to fulfill.";
    return false;
  }

  errorMessage = "";
  return true;
}

void sendJson(int statusCode, const String& jsonPayload) {
  server.send(statusCode, "application/json; charset=utf-8", jsonPayload);
}

void sendError(int statusCode, const String& message) {
  sendJson(statusCode, "{\"error\":\"" + jsonEscape(message) + "\"}");
}

uint64_t timestampSortKey(const String& value) {
  String trimmed = trimCopy(value);
  if (trimmed.isEmpty()) {
    return 0;
  }

  if (trimmed.startsWith("UPTIME+")) {
    int end = trimmed.indexOf('s', 7);
    if (end < 0) {
      end = trimmed.length();
    }
    return static_cast<uint64_t>(trimmed.substring(7, end).toInt());
  }

  String digits = "";
  digits.reserve(trimmed.length());
  for (size_t i = 0; i < trimmed.length(); ++i) {
    const char c = trimmed.charAt(i);
    if (isDigit(c)) {
      digits += c;
    }
  }

  if (digits.isEmpty()) {
    return 0;
  }

  return strtoull(digits.c_str(), nullptr, 10);
}

String serializeCloudConfigLine(const CloudBackupConfig& config) {
  String line;
  line.reserve(512);
  line += sanitizeField(config.provider);
  line += '|';
  line += sanitizeField(config.loginEmail);
  line += '|';
  line += sanitizeField(config.folderName);
  line += '|';
  line += sanitizeField(config.folderHint);
  line += '|';
  line += sanitizeField(config.mode);
  line += '|';
  line += sanitizeField(config.backupMode);
  line += '|';
  line += sanitizeField(config.assetMode);
  line += '|';
  line += sanitizeField(config.brandName);
  line += '|';
  line += sanitizeField(config.brandLogoRef);
  line += '|';
  line += sanitizeField(config.clientId);
  line += '|';
  line += sanitizeField(config.clientSecret);
  line += '|';
  line += sanitizeField(config.updatedAt);
  return line;
}

String serializeGoogleDriveStateLine(const GoogleDriveState& state) {
  String line;
  line.reserve(512);
  line += sanitizeField(state.refreshToken);
  line += '|';
  line += sanitizeField(state.folderId);
  line += '|';
  line += sanitizeField(state.lastSyncAt);
  line += '|';
  line += sanitizeField(state.lastSyncedManifestHash);
  line += '|';
  line += sanitizeField(state.lastSyncedSnapshotAt);
  line += '|';
  line += sanitizeField(state.localSnapshotAt);
  line += '|';
  line += sanitizeField(state.authStatus);
  line += '|';
  line += sanitizeField(state.syncStatus);
  line += '|';
  line += sanitizeField(state.lastError);
  return line;
}

String loadPreferenceString(const char* key) {
  if (!g_preferencesReady) {
    return "";
  }
  return g_preferences.getString(key, "");
}

bool savePreferenceString(const char* key, const String& value) {
  if (!g_preferencesReady) {
    return false;
  }
  return g_preferences.putString(key, value) > 0;
}

bool savePreferenceValueOrClear(const char* key, const String& value) {
  if (!g_preferencesReady) {
    return false;
  }

  if (value.isEmpty()) {
    g_preferences.remove(key);
    return true;
  }

  g_preferences.putString(key, value);
  return true;
}

bool loadWifiConfig() {
  g_wifiConfig = defaultWifiConfig();
  if (!g_preferencesReady) {
    return false;
  }

  g_wifiConfig.ssid = loadPreferenceString(PREFS_WIFI_SSID_KEY);
  g_wifiConfig.password = loadPreferenceString(PREFS_WIFI_PASS_KEY);
  g_wifiConfig.updatedAt = loadPreferenceString(PREFS_WIFI_UPDATED_KEY);
  return wifiConfigUsable(g_wifiConfig);
}

bool saveWifiConfig() {
  bool ok = true;
  ok = savePreferenceValueOrClear(PREFS_WIFI_SSID_KEY, g_wifiConfig.ssid) && ok;
  ok = savePreferenceValueOrClear(PREFS_WIFI_PASS_KEY, g_wifiConfig.password) && ok;
  ok = savePreferenceValueOrClear(PREFS_WIFI_UPDATED_KEY, g_wifiConfig.updatedAt) && ok;
  return ok;
}

void clearWifiConfig() {
  g_wifiConfig = defaultWifiConfig();
  saveWifiConfig();
}

bool ensureFileHeader(const char* path, const char* header) {
  if (!g_sdReady) {
    return false;
  }

  bool rewrite = true;
  File existing = storageFs().open(path, FILE_READ);
  if (existing) {
    String firstLine = existing.readStringUntil('\n');
    firstLine.trim();
    existing.close();
    rewrite = firstLine != String(header);
  }

  if (!rewrite) {
    return true;
  }

  File f = storageFs().open(path, FILE_WRITE);
  if (!f) {
    return false;
  }

  f.println(header);
  f.close();
  return true;
}

void ensureDataFiles() {
  fs::FS& fs = storageFs();

  if (!fs.exists(IMAGE_DIR)) {
    fs.mkdir(IMAGE_DIR);
  }

  if (!fs.exists(INVENTORY_FILE)) {
    File f = fs.open(INVENTORY_FILE, FILE_WRITE);
    if (f) {
      f.println(inventoryHeaderLine());
      f.close();
    }
  }

  if (!fs.exists(TRANSACTION_FILE)) {
    File f = fs.open(TRANSACTION_FILE, FILE_WRITE);
    if (f) {
      f.println("timestamp|item_id|action|delta|qty_after|note");
      f.close();
    }
  }

  if (!fs.exists(ORDERS_FILE)) {
    File f = fs.open(ORDERS_FILE, FILE_WRITE);
    if (f) {
      f.print("{\"orders\":[]}");
      f.close();
    }
  }

  ensureFileHeader(DEVICE_LOG_FILE, "timestamp|mac_address|uptime_seconds|event|detail");
}

bool parseInventoryLine(const String& line, ItemRecord& outItem) {
  const std::vector<String> fields = splitPipeLine(line);
  if (fields.size() < 4) {
    return false;
  }

  const String id = trimCopy(fields[0]);
  int32_t qty = 0;
  if (id.isEmpty()) {
    return false;
  }

  outItem.id = id;
  outItem.category = String(DEFAULT_CATEGORY);
  outItem.partName = "";
  outItem.qrCode = "";
  outItem.color = "";
  outItem.material = "";
  outItem.qty = 0;
  outItem.imageRef = "";
  outItem.bomProduct = "";
  outItem.bomQty = 0;
  outItem.updatedAt = "";

  if (fields.size() >= 11) {
    qty = static_cast<int32_t>(fields[6].toInt());
    if (qty < 0) {
      return false;
    }

    const int32_t bomQty = static_cast<int32_t>(fields[9].toInt());
    if (bomQty < 0) {
      return false;
    }

    outItem.category = normalizeCategory(fields[1]);
    outItem.partName = fields[2];
    outItem.qrCode = fields[3];
    outItem.color = fields[4];
    outItem.material = fields[5];
    outItem.qty = qty;
    outItem.imageRef = fields[7];
    outItem.bomProduct = fields[8];
    outItem.bomQty = bomQty;
    outItem.updatedAt = fields[10];
    return true;
  }

  if (fields.size() >= 10) {
    qty = static_cast<int32_t>(fields[7].toInt());
    if (qty < 0) {
      return false;
    }

    outItem.category = normalizeCategory(fields[1]);
    outItem.partName = fields[2];
    outItem.id = !trimCopy(fields[3]).isEmpty() ? trimCopy(fields[3]) : id;
    outItem.qrCode = fields[4];
    outItem.color = fields[5];
    outItem.material = fields[6];
    outItem.qty = qty;
    outItem.imageRef = fields[8];
    outItem.bomProduct = "";
    outItem.bomQty = 0;
    outItem.updatedAt = fields[9];
    return true;
  }

  qty = static_cast<int32_t>(fields[2].toInt());
  if (qty < 0) {
    return false;
  }

  outItem.category = String(DEFAULT_CATEGORY);
  outItem.partName = fields[1];
  outItem.qrCode = "";
  outItem.color = "";
  outItem.material = "";
  outItem.qty = qty;
  outItem.imageRef = "";
  outItem.bomProduct = "";
  outItem.bomQty = 0;
  outItem.updatedAt = fields[3];
  return true;
}

bool saveInventory() {
  if (!g_sdReady) {
    return false;
  }

  fs::FS& fs = storageFs();
  File temp = fs.open(INVENTORY_TMP_FILE, FILE_WRITE);
  if (!temp) {
    return false;
  }

  temp.println(inventoryHeaderLine());
  for (const ItemRecord& item : g_items) {
    temp.print(sanitizeField(item.id));
    temp.print('|');
    temp.print(sanitizeField(normalizeCategory(item.category)));
    temp.print('|');
    temp.print(sanitizeField(item.partName));
    temp.print('|');
    temp.print(sanitizeField(item.qrCode));
    temp.print('|');
    temp.print(sanitizeField(item.color));
    temp.print('|');
    temp.print(sanitizeField(item.material));
    temp.print('|');
    temp.print(String(item.qty));
    temp.print('|');
    temp.print(sanitizeField(item.imageRef));
    temp.print('|');
    temp.print(sanitizeField(item.bomProduct));
    temp.print('|');
    temp.print(String(item.bomQty));
    temp.print('|');
    temp.println(sanitizeField(item.updatedAt));
  }
  temp.close();

  fs.remove(INVENTORY_FILE);
  if (!fs.rename(INVENTORY_TMP_FILE, INVENTORY_FILE)) {
    return false;
  }

  return true;
}

bool loadInventory() {
  g_inventoryLoadHealthy = false;
  if (!g_sdReady) {
    return false;
  }

  ensureDataFiles();
  g_items.clear();

  File f = storageFs().open(INVENTORY_FILE, FILE_READ);
  if (!f) {
    return false;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line.startsWith("part_number|") || line.startsWith("id|")) {
      continue;
    }

    ItemRecord item;
    if (parseInventoryLine(line, item)) {
      g_items.push_back(item);
    }
  }
  f.close();

  std::sort(g_items.begin(), g_items.end(), [](const ItemRecord& a, const ItemRecord& b) {
    return normalizeLookupValue(a.id) < normalizeLookupValue(b.id);
  });

  g_inventoryLoadHealthy = true;
  return true;
}

bool parseCloudConfigLine(const String& line, CloudBackupConfig& outConfig) {
  const std::vector<String> fields = splitPipeLine(line);
  if (fields.size() < 6) {
    return false;
  }

  outConfig = defaultCloudBackupConfig();
  outConfig.provider = trimCopy(fields[0]);
  outConfig.loginEmail = fields[1];
  outConfig.folderName = fields[2];
  outConfig.folderHint = fields[3];
  outConfig.mode = trimCopy(fields[4]);
  if (fields.size() >= 12) {
    outConfig.backupMode = trimCopy(fields[5]);
    outConfig.assetMode = trimCopy(fields[6]);
    outConfig.brandName = fields[7];
    outConfig.brandLogoRef = fields[8];
    outConfig.clientId = fields[9];
    outConfig.clientSecret = fields[10];
    outConfig.updatedAt = fields[11];
  } else if (fields.size() >= 10) {
    outConfig.backupMode = trimCopy(fields[5]);
    outConfig.assetMode = trimCopy(fields[6]);
    outConfig.brandName = fields[7];
    outConfig.brandLogoRef = fields[8];
    outConfig.updatedAt = fields[9];
  } else {
    outConfig.updatedAt = fields[5];
  }

  if (outConfig.provider.isEmpty()) {
    outConfig.provider = "google_drive";
  }
  if (outConfig.mode.isEmpty()) {
    outConfig.mode = "select_or_create";
  }
  if (outConfig.backupMode.isEmpty()) {
    outConfig.backupMode = "sd_only";
  }
  if (outConfig.assetMode.isEmpty()) {
    outConfig.assetMode = "sd_only";
  }
  if (outConfig.brandName.isEmpty()) {
    outConfig.brandName = "Stingray Inventory";
  }

  return true;
}

bool parseGoogleDriveStateLine(const String& line, GoogleDriveState& outState) {
  const std::vector<String> fields = splitPipeLine(line);
  if (fields.size() < 9) {
    return false;
  }

  outState = defaultGoogleDriveState();
  outState.refreshToken = fields[0];
  outState.folderId = fields[1];
  outState.lastSyncAt = fields[2];
  outState.lastSyncedManifestHash = fields[3];
  outState.lastSyncedSnapshotAt = fields[4];
  outState.localSnapshotAt = fields[5];
  outState.authStatus = fields[6].isEmpty() ? (outState.refreshToken.isEmpty() ? "disconnected" : "authorized") : fields[6];
  outState.syncStatus = fields[7].isEmpty() ? "idle" : fields[7];
  outState.lastError = fields[8];
  if (!outState.refreshToken.isEmpty()) {
    outState.authStatus = "authorized";
  }
  return true;
}

void clearPendingGoogleDeviceFlow() {
  g_googleDriveState.deviceCode = "";
  g_googleDriveState.userCode = "";
  g_googleDriveState.verificationUrl = "";
  g_googleDriveState.devicePollIntervalSeconds = 5;
}

bool loadCloudBackupConfig() {
  g_cloudBackupConfig = defaultCloudBackupConfig();
  bool loaded = false;

  const String prefsLine = loadPreferenceString(PREFS_CLOUD_CONFIG_KEY);
  if (!prefsLine.isEmpty()) {
    CloudBackupConfig parsed;
    if (parseCloudConfigLine(prefsLine, parsed)) {
      g_cloudBackupConfig = parsed;
      loaded = true;
    }
  }

  if (g_sdReady) {
    ensureDataFiles();
    File f = storageFs().open(CLOUD_CONFIG_FILE, FILE_READ);
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("provider|")) {
          continue;
        }

        CloudBackupConfig parsed;
        if (parseCloudConfigLine(line, parsed)) {
          if (!loaded || timestampSortKey(parsed.updatedAt) >= timestampSortKey(g_cloudBackupConfig.updatedAt)) {
            g_cloudBackupConfig = parsed;
          }
          loaded = true;
          break;
        }
      }
      f.close();
    }
  }

  return loaded;
}

bool saveCloudBackupConfig() {
  bool wroteAny = false;
  const String serialized = serializeCloudConfigLine(g_cloudBackupConfig);
  if (g_preferencesReady) {
    wroteAny = savePreferenceString(PREFS_CLOUD_CONFIG_KEY, serialized) || wroteAny;
  }

  if (!g_sdReady) {
    return wroteAny;
  }

  File temp = storageFs().open(CLOUD_CONFIG_TMP_FILE, FILE_WRITE);
  if (!temp) {
    return false;
  }

  temp.println(cloudConfigHeaderLine());
  temp.println(serialized);
  temp.close();

  storageFs().remove(CLOUD_CONFIG_FILE);
  if (!storageFs().rename(CLOUD_CONFIG_TMP_FILE, CLOUD_CONFIG_FILE)) {
    return false;
  }

  return true;
}

void enforceStandaloneSdBaseline() {
  if (!g_preferencesReady || loadPreferenceString(PREFS_SD_BASELINE_KEY) == "1") {
    return;
  }

  bool changed = false;
  if (g_cloudBackupConfig.backupMode != "sd_only") {
    g_cloudBackupConfig.backupMode = "sd_only";
    changed = true;
  }
  if (g_cloudBackupConfig.assetMode != "sd_only") {
    g_cloudBackupConfig.assetMode = "sd_only";
    changed = true;
  }

  if (changed) {
    g_cloudBackupConfig.updatedAt = currentTimestamp();
    saveCloudBackupConfig();
    markCloudDirty("standalone_baseline");
    appendDeviceLog("info", "standalone_mode", "Applied one-time SD-only baseline defaults.");
  }

  savePreferenceString(PREFS_SD_BASELINE_KEY, "1");
}

bool loadGoogleDriveState() {
  g_googleDriveState = defaultGoogleDriveState();
  bool loaded = false;

  const String prefsLine = loadPreferenceString(PREFS_GOOGLE_STATE_KEY);
  if (!prefsLine.isEmpty()) {
    GoogleDriveState parsed;
    if (parseGoogleDriveStateLine(prefsLine, parsed)) {
      g_googleDriveState = parsed;
      loaded = true;
    }
  }

  if (g_sdReady) {
    File f = storageFs().open(GOOGLE_STATE_FILE, FILE_READ);
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("refresh_token|")) {
          continue;
        }

        GoogleDriveState parsed;
        if (parseGoogleDriveStateLine(line, parsed)) {
          if (!loaded || timestampSortKey(parsed.lastSyncAt) >= timestampSortKey(g_googleDriveState.lastSyncAt)) {
            g_googleDriveState = parsed;
          }
          loaded = true;
          break;
        }
      }
      f.close();
    }
  }

  clearPendingGoogleDeviceFlow();
  return loaded;
}

bool saveGoogleDriveState() {
  bool wroteAny = false;
  const String serialized = serializeGoogleDriveStateLine(g_googleDriveState);
  if (g_preferencesReady) {
    wroteAny = savePreferenceString(PREFS_GOOGLE_STATE_KEY, serialized) || wroteAny;
  }

  if (!g_sdReady) {
    return wroteAny;
  }

  File temp = storageFs().open(GOOGLE_STATE_TMP_FILE, FILE_WRITE);
  if (!temp) {
    return false;
  }

  temp.println(googleDriveStateHeaderLine());
  temp.println(serialized);
  temp.close();

  storageFs().remove(GOOGLE_STATE_FILE);
  if (!storageFs().rename(GOOGLE_STATE_TMP_FILE, GOOGLE_STATE_FILE)) {
    return false;
  }

  return true;
}

String cloudBackupConfigToJson() {
  String json = "{";
  json += "\"provider\":\"" + jsonEscape(g_cloudBackupConfig.provider) + "\"";
  json += ",\"login_email\":\"" + jsonEscape(g_cloudBackupConfig.loginEmail) + "\"";
  json += ",\"folder_name\":\"" + jsonEscape(g_cloudBackupConfig.folderName) + "\"";
  json += ",\"folder_hint\":\"" + jsonEscape(g_cloudBackupConfig.folderHint) + "\"";
  json += ",\"mode\":\"" + jsonEscape(g_cloudBackupConfig.mode) + "\"";
  json += ",\"backup_mode\":\"" + jsonEscape(g_cloudBackupConfig.backupMode) + "\"";
  json += ",\"asset_mode\":\"" + jsonEscape(g_cloudBackupConfig.assetMode) + "\"";
  json += ",\"brand_name\":\"" + jsonEscape(g_cloudBackupConfig.brandName) + "\"";
  json += ",\"brand_logo_ref\":\"" + jsonEscape(g_cloudBackupConfig.brandLogoRef) + "\"";
  json += ",\"client_id\":\"" + jsonEscape(g_cloudBackupConfig.clientId) + "\"";
  json += ",\"client_secret\":\"" + jsonEscape(g_cloudBackupConfig.clientSecret) + "\"";
  json += ",\"updated_at\":\"" + jsonEscape(g_cloudBackupConfig.updatedAt) + "\"";
  json += ",\"auth_ready\":" + String(!g_googleDriveState.refreshToken.isEmpty() ? "true" : "false");
  json += ",\"auth_status\":\"" + jsonEscape(g_googleDriveState.authStatus) + "\"";
  json += ",\"sync_status\":\"" + jsonEscape(g_googleDriveState.syncStatus) + "\"";
  json += ",\"last_error\":\"" + jsonEscape(g_googleDriveState.lastError) + "\"";
  json += ",\"verification_url\":\"" + jsonEscape(g_googleDriveState.verificationUrl) + "\"";
  json += ",\"user_code\":\"" + jsonEscape(g_googleDriveState.userCode) + "\"";
  json += ",\"device_poll_interval\":" + String(g_googleDriveState.devicePollIntervalSeconds);
  json += ",\"folder_id\":\"" + jsonEscape(g_googleDriveState.folderId) + "\"";
  json += ",\"last_sync_at\":\"" + jsonEscape(g_googleDriveState.lastSyncAt) + "\"";
  json += ",\"last_synced_snapshot_at\":\"" + jsonEscape(g_googleDriveState.lastSyncedSnapshotAt) + "\"";
  json += ",\"local_snapshot_at\":\"" + jsonEscape(g_googleDriveState.localSnapshotAt) + "\"";
  json += ",\"drive_scope\":\"" + jsonEscape(String(GOOGLE_DRIVE_SCOPE)) + "\"";
  json += ",\"sd_ready\":" + String(g_sdReady ? "true" : "false");
  json += "}";
  return json;
}

String jsonUnescape(const String& value) {
  String out;
  out.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (c != '\\' || i + 1 >= value.length()) {
      out += c;
      continue;
    }

    const char next = value.charAt(i + 1);
    if (next == '"' || next == '\\' || next == '/') {
      out += next;
      ++i;
      continue;
    }
    if (next == 'b') {
      out += '\b';
      ++i;
      continue;
    }
    if (next == 'f') {
      out += '\f';
      ++i;
      continue;
    }
    if (next == 'n') {
      out += '\n';
      ++i;
      continue;
    }
    if (next == 'r') {
      out += '\r';
      ++i;
      continue;
    }
    if (next == 't') {
      out += '\t';
      ++i;
      continue;
    }

    out += next;
    ++i;
  }

  return out;
}

String jsonStringField(const String& json, const char* key) {
  const String needle = "\"" + String(key) + "\"";
  const int keyPos = json.indexOf(needle);
  if (keyPos < 0) {
    return "";
  }

  int colonPos = json.indexOf(':', keyPos + needle.length());
  if (colonPos < 0) {
    return "";
  }

  int valuePos = colonPos + 1;
  while (valuePos < json.length() && isspace(static_cast<unsigned char>(json.charAt(valuePos)))) {
    ++valuePos;
  }

  if (valuePos >= json.length() || json.charAt(valuePos) != '"') {
    return "";
  }

  ++valuePos;
  String raw = "";
  bool escaping = false;
  for (int i = valuePos; i < json.length(); ++i) {
    const char c = json.charAt(i);
    if (!escaping && c == '"') {
      return jsonUnescape(raw);
    }
    if (c == '\\' && !escaping) {
      escaping = true;
      raw += c;
      continue;
    }
    escaping = false;
    raw += c;
  }

  return "";
}

long jsonLongField(const String& json, const char* key, long fallback = 0) {
  const String needle = "\"" + String(key) + "\"";
  const int keyPos = json.indexOf(needle);
  if (keyPos < 0) {
    return fallback;
  }

  int colonPos = json.indexOf(':', keyPos + needle.length());
  if (colonPos < 0) {
    return fallback;
  }

  int valuePos = colonPos + 1;
  while (valuePos < json.length() && isspace(static_cast<unsigned char>(json.charAt(valuePos)))) {
    ++valuePos;
  }

  int endPos = valuePos;
  while (endPos < json.length() && (isDigit(json.charAt(endPos)) || json.charAt(endPos) == '-')) {
    ++endPos;
  }

  if (endPos <= valuePos) {
    return fallback;
  }

  return json.substring(valuePos, endPos).toInt();
}

bool wifiConnectedForCloud() {
  return WiFi.status() == WL_CONNECTED;
}

bool googleCloudRequested() {
  return g_cloudBackupConfig.provider == "google_drive" &&
    (g_cloudBackupConfig.backupMode != "sd_only" || g_cloudBackupConfig.assetMode != "sd_only");
}

bool googleCredentialsConfigured() {
  return !trimCopy(g_cloudBackupConfig.clientId).isEmpty() && !trimCopy(g_cloudBackupConfig.clientSecret).isEmpty();
}

bool googleAuthorized() {
  return !trimCopy(g_googleDriveState.refreshToken).isEmpty();
}

void updateGoogleState(const String& authStatus, const String& syncStatus, const String& lastError = "") {
  if (!authStatus.isEmpty()) {
    g_googleDriveState.authStatus = authStatus;
  }
  if (!syncStatus.isEmpty()) {
    g_googleDriveState.syncStatus = syncStatus;
  }
  g_googleDriveState.lastError = lastError;
  saveGoogleDriveState();
}

String folderIdFromHint(String hint) {
  hint.trim();
  if (hint.isEmpty()) {
    return "";
  }

  const int foldersPos = hint.indexOf("/folders/");
  if (foldersPos >= 0) {
    String id = hint.substring(foldersPos + 9);
    const int slashPos = id.indexOf('/');
    if (slashPos >= 0) {
      id = id.substring(0, slashPos);
    }
    const int queryPos = id.indexOf('?');
    if (queryPos >= 0) {
      id = id.substring(0, queryPos);
    }
    id.trim();
    return id;
  }

  const int idPos = hint.indexOf("id=");
  if (idPos >= 0) {
    String id = hint.substring(idPos + 3);
    const int ampPos = id.indexOf('&');
    if (ampPos >= 0) {
      id = id.substring(0, ampPos);
    }
    id.trim();
    return id;
  }

  if (hint.indexOf('/') < 0 && hint.indexOf(' ') < 0) {
    return hint;
  }

  return "";
}

void resetImageUploadState() {
  if (g_uploadFile) {
    g_uploadFile.close();
  }
  if (!g_uploadStoredPath.isEmpty() && !g_uploadError.isEmpty()) {
    storageFs().remove(g_uploadStoredPath);
  }
  g_uploadStoredPath = "";
  g_uploadBytesWritten = 0;
}

String normalizedDeviceLogEvent(const String& event) {
  if (event == "boot") {
    return "startup";
  }
  if (event == "reset") {
    return "reset";
  }
  if (event == "item_created") {
    return "inventory_create";
  }
  if (event == "item_removed") {
    return "inventory_remove";
  }
  if (event == "item_adjusted") {
    return "inventory_adjust";
  }
  if (event == "item_set_qty") {
    return "inventory_set_qty";
  }
  return "";
}

void appendDeviceLog(const String& level, const String& event, const String& detail) {
  (void)level;
  if (!g_sdReady) {
    return;
  }

  const String normalizedEvent = normalizedDeviceLogEvent(event);
  if (normalizedEvent.isEmpty()) {
    return;
  }

  File f = storageFs().open(DEVICE_LOG_FILE, FILE_APPEND);
  if (!f) {
    return;
  }

  String finalDetail = detail;
  if (normalizedEvent == "startup") {
    finalDetail = "startup complete; reset_reason=" + g_resetReason;
  } else if (normalizedEvent == "reset") {
    finalDetail = "reset_reason=" + detail;
  }

  f.print(sanitizeField(currentTimestamp()));
  f.print('|');
  f.print(sanitizeField(g_macAddress));
  f.print('|');
  f.print(String(millis() / 1000));
  f.print('|');
  f.print(sanitizeField(normalizedEvent));
  f.print('|');
  f.println(sanitizeField(finalDetail));
  f.close();
}

void appendTimeLog(const String& event, const String& detail) {
  (void)event;
  (void)detail;
}

String hashHex32(uint32_t value) {
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%08X", static_cast<unsigned int>(value));
  return String(buffer);
}

String hashText(const String& value) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < value.length(); ++i) {
    hash ^= static_cast<uint8_t>(value.charAt(i));
    hash *= 16777619u;
  }
  return hashHex32(hash);
}

String hashFileAtPath(const String& path, size_t& outSize) {
  outSize = 0;
  if (!g_sdReady) {
    return "";
  }

  File f = storageFs().open(path, FILE_READ);
  if (!f) {
    return "";
  }

  uint32_t hash = 2166136261u;
  uint8_t buffer[512];
  while (f.available()) {
    const size_t read = f.read(buffer, sizeof(buffer));
    outSize += read;
    for (size_t i = 0; i < read; ++i) {
      hash ^= buffer[i];
      hash *= 16777619u;
    }
  }
  f.close();

  return hashHex32(hash);
}

String remoteNameForPath(String path) {
  path.trim();
  if (path.startsWith("/")) {
    path.remove(0, 1);
  }
  path.replace("/", "__");
  return "stingray__" + path;
}

bool ensureDirectoryTree(const String& path) {
  if (!g_sdReady) {
    return false;
  }

  int slashPos = path.indexOf('/', 1);
  while (slashPos >= 0) {
    const String dir = path.substring(0, slashPos);
    if (!dir.isEmpty() && !storageFs().exists(dir)) {
      storageFs().mkdir(dir);
    }
    slashPos = path.indexOf('/', slashPos + 1);
  }

  return true;
}

String deriveLocalSnapshotAt() {
  String latest = g_cloudBackupConfig.updatedAt;
  uint64_t latestKey = timestampSortKey(latest);

  for (size_t i = 0; i < g_items.size(); ++i) {
    const uint64_t itemKey = timestampSortKey(g_items[i].updatedAt);
    if (itemKey > latestKey) {
      latestKey = itemKey;
      latest = g_items[i].updatedAt;
    }
  }

  if (latest.isEmpty()) {
    latest = currentTimestamp();
  }
  return latest;
}

void markCloudDirty(const String& reason) {
  g_googleDriveState.localSnapshotAt = currentTimestamp();
  if (g_googleDriveState.syncStatus != "restoring") {
    g_googleDriveState.syncStatus = "local_dirty";
  }
  if (!reason.isEmpty() && g_sdReady) {
    appendDeviceLog("info", "cloud_dirty", reason);
  }
  saveGoogleDriveState();
}

void maybeAddBackupEntry(const String& path, std::vector<BackupFileEntry>& outEntries) {
  if (!g_sdReady || !storageFs().exists(path)) {
    return;
  }

  BackupFileEntry entry;
  entry.path = path;
  entry.mimeType = contentTypeForPath(path);
  entry.hash = hashFileAtPath(path, entry.size);
  if (entry.hash.isEmpty()) {
    return;
  }
  entry.remoteName = remoteNameForPath(path);
  outEntries.push_back(entry);
}

void collectBackupEntriesFromDirectory(const String& dirPath, std::vector<BackupFileEntry>& outEntries) {
  if (!g_sdReady || !storageFs().exists(dirPath)) {
    return;
  }

  File dir = storageFs().open(dirPath, FILE_READ);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  while (true) {
    File entryFile = dir.openNextFile();
    if (!entryFile) {
      break;
    }

    String path = entryFile.name();
    if (!path.startsWith("/")) {
      path = dirPath;
      if (!path.endsWith("/")) {
        path += "/";
      }
      path += entryFile.name();
    }

    if (entryFile.isDirectory()) {
      entryFile.close();
      collectBackupEntriesFromDirectory(path, outEntries);
      continue;
    }

    entryFile.close();
    maybeAddBackupEntry(path, outEntries);
  }

  dir.close();
}

void buildBackupEntries(std::vector<BackupFileEntry>& outEntries) {
  outEntries.clear();
  maybeAddBackupEntry(INVENTORY_FILE, outEntries);
  maybeAddBackupEntry(ORDERS_FILE, outEntries);
  maybeAddBackupEntry(CLOUD_CONFIG_FILE, outEntries);
  maybeAddBackupEntry(UI_INDEX_FILE, outEntries);
  maybeAddBackupEntry(UI_ITEM_FILE, outEntries);
  maybeAddBackupEntry(UI_VERSION_FILE, outEntries);
  collectBackupEntriesFromDirectory(IMAGE_DIR, outEntries);

  std::sort(outEntries.begin(), outEntries.end(), [](const BackupFileEntry& a, const BackupFileEntry& b) {
    return a.path < b.path;
  });
}

String backupManifestToText(const BackupManifest& manifest) {
  String text = "version=1\n";
  text += "snapshot_at=" + sanitizeField(manifest.snapshotAt) + "\n";
  text += "path|mime|size|hash|remote_name\n";
  for (size_t i = 0; i < manifest.entries.size(); ++i) {
    const BackupFileEntry& entry = manifest.entries[i];
    text += sanitizeField(entry.path);
    text += '|';
    text += sanitizeField(entry.mimeType);
    text += '|';
    text += String(entry.size);
    text += '|';
    text += sanitizeField(entry.hash);
    text += '|';
    text += sanitizeField(entry.remoteName);
    text += '\n';
  }
  return text;
}

bool buildLocalBackupManifest(BackupManifest& manifest) {
  if (!g_sdReady) {
    return false;
  }

  manifest = BackupManifest();
  buildBackupEntries(manifest.entries);
  manifest.snapshotAt = !g_googleDriveState.localSnapshotAt.isEmpty() ? g_googleDriveState.localSnapshotAt : deriveLocalSnapshotAt();
  manifest.manifestHash = hashText(backupManifestToText(manifest));
  return !manifest.entries.empty();
}

bool parseBackupManifestText(const String& text, BackupManifest& manifest) {
  manifest = BackupManifest();
  int start = 0;
  bool sawHeader = false;

  while (start <= text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) {
      end = text.length();
    }

    String line = text.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.isEmpty()) {
      continue;
    }

    if (line == "version=1") {
      continue;
    }
    if (line.startsWith("snapshot_at=")) {
      manifest.snapshotAt = line.substring(12);
      continue;
    }
    if (line == "path|mime|size|hash|remote_name") {
      sawHeader = true;
      continue;
    }
    if (!sawHeader) {
      continue;
    }

    const std::vector<String> fields = splitPipeLine(line);
    if (fields.size() < 5) {
      continue;
    }

    BackupFileEntry entry;
    entry.path = fields[0];
    entry.mimeType = fields[1];
    entry.size = static_cast<size_t>(fields[2].toInt());
    entry.hash = fields[3];
    entry.remoteName = fields[4];
    manifest.entries.push_back(entry);
  }

  manifest.manifestHash = hashText(backupManifestToText(manifest));
  return !manifest.entries.empty();
}

String tailLogJson(const char* path, size_t limit = 40) {
  String json = "{\"lines\":[";
  if (!g_sdReady) {
    json += "]}";
    return json;
  }

  File f = storageFs().open(path, FILE_READ);
  if (!f) {
    json += "]}";
    return json;
  }

  std::vector<String> lines;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) {
      continue;
    }
    if (line.startsWith("timestamp|")) {
      continue;
    }
    lines.push_back(line);
  }
  f.close();

  const size_t start = lines.size() > limit ? lines.size() - limit : 0;
  bool first = true;
  for (size_t i = start; i < lines.size(); ++i) {
    if (!first) {
      json += ",";
    }
    json += "\"" + jsonEscape(lines[i]) + "\"";
    first = false;
  }
  json += "]}";
  return json;
}

void appendTransaction(const String& itemId, const char* action, int32_t delta, int32_t qtyAfter, const String& note) {
  if (!g_sdReady) {
    return;
  }

  File f = storageFs().open(TRANSACTION_FILE, FILE_APPEND);
  if (!f) {
    return;
  }

  f.print(sanitizeField(currentTimestamp()));
  f.print('|');
  f.print(sanitizeField(itemId));
  f.print('|');
  f.print(sanitizeField(String(action)));
  f.print('|');
  f.print(String(delta));
  f.print('|');
  f.print(String(qtyAfter));
  f.print('|');
  f.println(sanitizeField(note));
  f.close();
}

bool itemCanHaveBom(const ItemRecord& item) {
  const String category = normalizeCategory(item.category);
  return category == "product" || category == "kit";
}

bool isBomComponentOf(const ItemRecord& component, const ItemRecord& parent) {
  if (normalizeCategory(component.category) != "part") {
    return false;
  }

  const String componentParent = normalizeLookupValue(component.bomProduct);
  const String parentId = normalizeLookupValue(parent.id);
  const String parentName = normalizeLookupValue(parent.partName);
  return !componentParent.isEmpty() && ((!parentId.isEmpty() && componentParent == parentId) || (!parentName.isEmpty() && componentParent == parentName));
}

String bomComponentsToJson(const ItemRecord& parent) {
  String json = "[";
  bool first = true;

  for (size_t i = 0; i < g_items.size(); ++i) {
    if (!isBomComponentOf(g_items[i], parent)) {
      continue;
    }

    if (!first) {
      json += ",";
    }
    json += itemToJson(g_items[i]);
    first = false;
  }

  json += "]";
  return json;
}

String itemToJson(const ItemRecord& item) {
  String json = "{";
  json += "\"id\":\"" + jsonEscape(item.id) + "\"";
  json += ",\"category\":\"" + jsonEscape(normalizeCategory(item.category)) + "\"";
  json += ",\"category_label\":\"" + jsonEscape(categoryLabel(normalizeCategory(item.category))) + "\"";
  json += ",\"part_name\":\"" + jsonEscape(item.partName) + "\"";
  json += ",\"qr_code\":\"" + jsonEscape(item.qrCode) + "\"";
  json += ",\"color\":\"" + jsonEscape(item.color) + "\"";
  json += ",\"material\":\"" + jsonEscape(item.material) + "\"";
  json += ",\"qty\":" + String(item.qty);
  json += ",\"image_ref\":\"" + jsonEscape(item.imageRef) + "\"";
  json += ",\"bom_product\":\"" + jsonEscape(item.bomProduct) + "\"";
  json += ",\"bom_qty\":" + String(item.bomQty);
  json += ",\"has_bom\":" + String(itemCanHaveBom(item) ? "true" : "false");
  json += ",\"updated_at\":\"" + jsonEscape(item.updatedAt) + "\"";
  json += ",\"stock_zero\":" + String(item.qty == 0 ? "true" : "false");
  json += ",\"qr_link\":\"" + jsonEscape(itemUrl(item.id)) + "\"";
  json += "}";
  return json;
}

String itemPayloadJson(const ItemRecord& item) {
  String payload = "{\"item\":";
  payload += itemToJson(item);
  payload += ",\"bom_components\":";
  payload += bomComponentsToJson(item);
  payload += "}";
  return payload;
}

bool performHttpRequest(
  const String& method,
  const String& url,
  const String& bearerToken,
  const String& contentType,
  const String& body,
  String& response,
  int& statusCode
) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    statusCode = -1;
    response = "";
    return false;
  }

  http.setTimeout(15000);
  if (!bearerToken.isEmpty()) {
    http.addHeader("Authorization", "Bearer " + bearerToken);
  }
  if (!contentType.isEmpty()) {
    http.addHeader("Content-Type", contentType);
  }

  int code = -1;
  if (body.length() > 0) {
    code = http.sendRequest(method.c_str(), reinterpret_cast<uint8_t*>(const_cast<char*>(body.c_str())), body.length());
  } else {
    code = http.sendRequest(method.c_str());
  }

  statusCode = code;
  response = code > 0 ? http.getString() : "";
  http.end();
  return code > 0;
}

bool performHttpStreamUpload(
  const String& method,
  const String& url,
  const String& bearerToken,
  const String& contentType,
  Stream& stream,
  size_t size,
  String& response,
  int& statusCode
) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    statusCode = -1;
    response = "";
    return false;
  }

  http.setTimeout(30000);
  if (!bearerToken.isEmpty()) {
    http.addHeader("Authorization", "Bearer " + bearerToken);
  }
  if (!contentType.isEmpty()) {
    http.addHeader("Content-Type", contentType);
  }

  statusCode = http.sendRequest(method.c_str(), &stream, size);
  response = statusCode > 0 ? http.getString() : "";
  http.end();
  return statusCode > 0;
}

bool performHttpDownloadToFile(const String& url, const String& bearerToken, const String& path, String& errorMessage) {
  if (!g_sdReady) {
    errorMessage = "Insert a working SD card before restoring backup files.";
    return false;
  }

  ensureDirectoryTree(path);
  const String tempPath = path + ".tmp";
  storageFs().remove(tempPath);

  File out = storageFs().open(tempPath, FILE_WRITE);
  if (!out) {
    errorMessage = "Failed to open " + path + " for restore.";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    out.close();
    storageFs().remove(tempPath);
    errorMessage = "Failed to open secure connection to Google Drive.";
    return false;
  }

  http.setTimeout(30000);
  if (!bearerToken.isEmpty()) {
    http.addHeader("Authorization", "Bearer " + bearerToken);
  }

  const int statusCode = http.GET();
  if (statusCode < 200 || statusCode >= 300) {
    const String response = http.getString();
    http.end();
    out.close();
    storageFs().remove(tempPath);
    errorMessage = response.isEmpty() ? "Google Drive download failed." : response;
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[512];
  while (http.connected() || stream->available()) {
    const size_t available = stream->available();
    if (!available) {
      delay(1);
      continue;
    }

    const size_t toRead = available > sizeof(buffer) ? sizeof(buffer) : available;
    const size_t count = stream->readBytes(buffer, toRead);
    if (!count) {
      continue;
    }
    out.write(buffer, count);
  }

  http.end();
  out.close();

  storageFs().remove(path);
  if (!storageFs().rename(tempPath, path)) {
    storageFs().remove(tempPath);
    errorMessage = "Failed to finalize restored file " + path + ".";
    return false;
  }

  return true;
}

String googleErrorMessageFromResponse(const String& response) {
  String description = jsonStringField(response, "error_description");
  if (!description.isEmpty()) {
    return description;
  }

  description = jsonStringField(response, "message");
  if (!description.isEmpty()) {
    return description;
  }

  description = jsonStringField(response, "error");
  if (!description.isEmpty()) {
    return description;
  }

  return response;
}

bool googleRefreshAccessToken(String& errorMessage) {
  if (!googleCredentialsConfigured()) {
    errorMessage = "Google client ID and secret are required.";
    updateGoogleState("credentials_missing", "idle", errorMessage);
    return false;
  }

  if (!googleAuthorized()) {
    errorMessage = "Google Drive is not linked yet.";
    updateGoogleState("disconnected", "idle", errorMessage);
    return false;
  }

  const uint32_t nowSeconds = millis() / 1000;
  if (!g_googleDriveState.accessToken.isEmpty() && nowSeconds + 30 < g_googleDriveState.accessTokenExpiresAt) {
    return true;
  }

  String response;
  int statusCode = 0;
  const String body =
    "client_id=" + urlEncode(g_cloudBackupConfig.clientId) +
    "&client_secret=" + urlEncode(g_cloudBackupConfig.clientSecret) +
    "&refresh_token=" + urlEncode(g_googleDriveState.refreshToken) +
    "&grant_type=refresh_token";

  if (!performHttpRequest("POST", GOOGLE_TOKEN_URL, "", "application/x-www-form-urlencoded", body, response, statusCode)) {
    errorMessage = "Failed to connect to Google's token endpoint.";
    updateGoogleState("error", "idle", errorMessage);
    return false;
  }

  if (statusCode < 200 || statusCode >= 300) {
    errorMessage = googleErrorMessageFromResponse(response);
    updateGoogleState("error", "idle", errorMessage);
    return false;
  }

  g_googleDriveState.accessToken = jsonStringField(response, "access_token");
  g_googleDriveState.tokenType = jsonStringField(response, "token_type");
  if (g_googleDriveState.tokenType.isEmpty()) {
    g_googleDriveState.tokenType = "Bearer";
  }
  g_googleDriveState.scope = jsonStringField(response, "scope");
  if (g_googleDriveState.scope.isEmpty()) {
    g_googleDriveState.scope = GOOGLE_DRIVE_SCOPE;
  }
  const long expiresIn = jsonLongField(response, "expires_in", 3600);
  g_googleDriveState.accessTokenExpiresAt = nowSeconds + static_cast<uint32_t>(expiresIn > 0 ? expiresIn : 3600);
  g_googleDriveState.authStatus = "authorized";
  g_googleDriveState.lastError = "";
  saveGoogleDriveState();
  return !g_googleDriveState.accessToken.isEmpty();
}

bool googleAuthorizedRequest(
  const String& method,
  const String& url,
  const String& contentType,
  const String& body,
  String& response,
  int& statusCode,
  String& errorMessage
) {
  if (!wifiConnectedForCloud()) {
    errorMessage = "WiFi is not connected.";
    return false;
  }

  if (!googleRefreshAccessToken(errorMessage)) {
    return false;
  }

  if (!performHttpRequest(method, url, g_googleDriveState.accessToken, contentType, body, response, statusCode)) {
    errorMessage = "Failed to connect to Google Drive.";
    return false;
  }

  if (statusCode == 401) {
    g_googleDriveState.accessToken = "";
    saveGoogleDriveState();
    if (!googleRefreshAccessToken(errorMessage)) {
      return false;
    }
    if (!performHttpRequest(method, url, g_googleDriveState.accessToken, contentType, body, response, statusCode)) {
      errorMessage = "Failed to reconnect to Google Drive.";
      return false;
    }
  }

  if (statusCode < 200 || statusCode >= 300) {
    errorMessage = googleErrorMessageFromResponse(response);
    return false;
  }

  return true;
}

bool googleAuthorizedStreamUpload(
  const String& method,
  const String& url,
  const String& contentType,
  Stream& stream,
  size_t size,
  String& response,
  int& statusCode,
  String& errorMessage
) {
  if (!wifiConnectedForCloud()) {
    errorMessage = "WiFi is not connected.";
    return false;
  }

  if (!googleRefreshAccessToken(errorMessage)) {
    return false;
  }

  if (!performHttpStreamUpload(method, url, g_googleDriveState.accessToken, contentType, stream, size, response, statusCode)) {
    errorMessage = "Failed to upload to Google Drive.";
    return false;
  }

  if (statusCode == 401) {
    g_googleDriveState.accessToken = "";
    saveGoogleDriveState();
    errorMessage = "Google authorization expired during upload. Retry sync.";
    return false;
  }

  if (statusCode < 200 || statusCode >= 300) {
    errorMessage = googleErrorMessageFromResponse(response);
    return false;
  }

  return true;
}

String googleDriveQueryLiteral(String value) {
  value.replace("\\", "\\\\");
  value.replace("'", "\\'");
  return value;
}

bool googleGetFileMetadata(const String& fileId, String& mimeType, String& errorMessage) {
  String response;
  int statusCode = 0;
  if (!googleAuthorizedRequest(
        "GET",
        String(GOOGLE_DRIVE_FILES_URL) + "/" + urlEncode(fileId) + "?fields=id,mimeType,name",
        "",
        "",
        response,
        statusCode,
        errorMessage)) {
    return false;
  }

  mimeType = jsonStringField(response, "mimeType");
  return true;
}

bool googleFindFileInFolder(const String& folderId, const String& fileName, const String& mimeFilter, String& fileId, String* modifiedTime, String& errorMessage) {
  fileId = "";
  if (modifiedTime) {
    *modifiedTime = "";
  }

  String query = "name='" + googleDriveQueryLiteral(fileName) + "' and trashed=false";
  if (!folderId.isEmpty()) {
    query += " and '" + folderId + "' in parents";
  }
  if (!mimeFilter.isEmpty()) {
    query += " and mimeType='" + googleDriveQueryLiteral(mimeFilter) + "'";
  }

  String response;
  int statusCode = 0;
  const String url =
    String(GOOGLE_DRIVE_FILES_URL) +
    "?pageSize=1&fields=files(id,name,mimeType,modifiedTime)&q=" + urlEncode(query);
  if (!googleAuthorizedRequest("GET", url, "", "", response, statusCode, errorMessage)) {
    return false;
  }

  fileId = jsonStringField(response, "id");
  if (modifiedTime) {
    *modifiedTime = jsonStringField(response, "modifiedTime");
  }
  return true;
}

bool googleCreateDriveFile(const String& name, const String& parentId, const String& mimeType, String& fileId, String& errorMessage) {
  String body = "{\"name\":\"" + jsonEscape(name) + "\"";
  if (!mimeType.isEmpty()) {
    body += ",\"mimeType\":\"" + jsonEscape(mimeType) + "\"";
  }
  if (!parentId.isEmpty()) {
    body += ",\"parents\":[\"" + jsonEscape(parentId) + "\"]";
  }
  body += "}";

  String response;
  int statusCode = 0;
  if (!googleAuthorizedRequest(
        "POST",
        String(GOOGLE_DRIVE_FILES_URL) + "?fields=id",
        "application/json",
        body,
        response,
        statusCode,
        errorMessage)) {
    return false;
  }

  fileId = jsonStringField(response, "id");
  if (fileId.isEmpty()) {
    errorMessage = "Google Drive did not return a file ID.";
    return false;
  }
  return true;
}

bool googleUploadDriveFileFromString(const String& fileId, const String& contentType, const String& content, String& errorMessage) {
  String response;
  int statusCode = 0;
  return googleAuthorizedRequest(
    "PATCH",
    String(GOOGLE_DRIVE_UPLOAD_URL) + "/" + urlEncode(fileId) + "?uploadType=media",
    contentType,
    content,
    response,
    statusCode,
    errorMessage
  );
}

bool googleUploadDriveFileFromStoragePath(const String& fileId, const String& path, const String& contentType, String& errorMessage) {
  if (!g_sdReady) {
    errorMessage = "SD card is not ready.";
    return false;
  }

  File file = storageFs().open(path, FILE_READ);
  if (!file) {
    errorMessage = "Could not open " + path + " for Google upload.";
    return false;
  }

  String response;
  int statusCode = 0;
  const size_t size = file.size();
  const String url = String(GOOGLE_DRIVE_UPLOAD_URL) + "/" + urlEncode(fileId) + "?uploadType=media";
  const bool ok = googleAuthorizedStreamUpload("PATCH", url, contentType, file, size, response, statusCode, errorMessage);
  file.close();
  return ok;
}

bool googleCreateOrUpdateFileFromString(const String& remoteName, const String& parentId, const String& mimeType, const String& content, String& errorMessage) {
  String fileId;
  if (!googleFindFileInFolder(parentId, remoteName, "", fileId, nullptr, errorMessage)) {
    return false;
  }
  if (fileId.isEmpty() && !googleCreateDriveFile(remoteName, parentId, mimeType, fileId, errorMessage)) {
    return false;
  }
  return googleUploadDriveFileFromString(fileId, mimeType, content, errorMessage);
}

bool googleCreateOrUpdateFileFromStoragePath(const String& remoteName, const String& parentId, const String& path, const String& mimeType, String& errorMessage) {
  String fileId;
  if (!googleFindFileInFolder(parentId, remoteName, "", fileId, nullptr, errorMessage)) {
    return false;
  }
  if (fileId.isEmpty() && !googleCreateDriveFile(remoteName, parentId, mimeType, fileId, errorMessage)) {
    return false;
  }
  return googleUploadDriveFileFromStoragePath(fileId, path, mimeType, errorMessage);
}

bool googleDownloadFileToString(const String& fileId, String& content, String& errorMessage) {
  String response;
  int statusCode = 0;
  if (!googleAuthorizedRequest(
        "GET",
        String(GOOGLE_DRIVE_FILES_URL) + "/" + urlEncode(fileId) + "?alt=media",
        "",
        "",
        response,
        statusCode,
        errorMessage)) {
    return false;
  }

  content = response;
  return true;
}

bool googleDownloadFileToStoragePath(const String& fileId, const String& path, String& errorMessage) {
  if (!wifiConnectedForCloud()) {
    errorMessage = "WiFi is not connected.";
    return false;
  }
  if (!googleRefreshAccessToken(errorMessage)) {
    return false;
  }

  if (!performHttpDownloadToFile(
        String(GOOGLE_DRIVE_FILES_URL) + "/" + urlEncode(fileId) + "?alt=media",
        g_googleDriveState.accessToken,
        path,
        errorMessage)) {
    return false;
  }

  return true;
}

bool googleResolveFolderId(String& folderId, String& errorMessage) {
  if (!trimCopy(g_googleDriveState.folderId).isEmpty()) {
    folderId = g_googleDriveState.folderId;
    return true;
  }

  String resolvedFolderId = folderIdFromHint(g_cloudBackupConfig.folderHint);
  if (!resolvedFolderId.isEmpty()) {
    String mimeType;
    if (googleGetFileMetadata(resolvedFolderId, mimeType, errorMessage) && mimeType == "application/vnd.google-apps.folder") {
      g_googleDriveState.folderId = resolvedFolderId;
      saveGoogleDriveState();
      folderId = resolvedFolderId;
      return true;
    }
    if (g_cloudBackupConfig.mode == "use_existing") {
      errorMessage = "The configured Google Drive folder could not be opened with this app.";
      return false;
    }
  }

  const String folderName = trimCopy(g_cloudBackupConfig.folderName).isEmpty()
    ? String("Stingray Inventory Backups")
    : trimCopy(g_cloudBackupConfig.folderName);

  String foundId;
  if (!googleFindFileInFolder("", folderName, "application/vnd.google-apps.folder", foundId, nullptr, errorMessage)) {
    return false;
  }

  if (foundId.isEmpty()) {
    if (g_cloudBackupConfig.mode == "use_existing") {
      errorMessage = "No matching Google Drive folder was found.";
      return false;
    }

    if (!googleCreateDriveFile(folderName, "", "application/vnd.google-apps.folder", foundId, errorMessage)) {
      return false;
    }
  }

  g_googleDriveState.folderId = foundId;
  saveGoogleDriveState();
  folderId = foundId;
  return !folderId.isEmpty();
}

bool googleDownloadManifest(BackupManifest& manifest, bool& found, String& errorMessage) {
  found = false;
  String folderId;
  if (!googleResolveFolderId(folderId, errorMessage)) {
    return false;
  }

  String manifestFileId;
  if (!googleFindFileInFolder(folderId, GOOGLE_MANIFEST_NAME, "", manifestFileId, nullptr, errorMessage)) {
    return false;
  }
  if (manifestFileId.isEmpty()) {
    return true;
  }

  String manifestText;
  if (!googleDownloadFileToString(manifestFileId, manifestText, errorMessage)) {
    return false;
  }
  if (!parseBackupManifestText(manifestText, manifest)) {
    errorMessage = "Google Drive manifest could not be parsed.";
    return false;
  }

  found = true;
  return true;
}

bool googleUploadManifest(const BackupManifest& manifest, const String& folderId, String& errorMessage) {
  return googleCreateOrUpdateFileFromString(
    GOOGLE_MANIFEST_NAME,
    folderId,
    "text/plain",
    backupManifestToText(manifest),
    errorMessage
  );
}

bool googleUploadBackupSnapshot(BackupManifest& manifest, String& errorMessage) {
  String folderId;
  if (!googleResolveFolderId(folderId, errorMessage)) {
    return false;
  }

  for (size_t i = 0; i < manifest.entries.size(); ++i) {
    const BackupFileEntry& entry = manifest.entries[i];
    if (!googleCreateOrUpdateFileFromStoragePath(entry.remoteName, folderId, entry.path, entry.mimeType, errorMessage)) {
      updateGoogleState("authorized", "upload_failed", errorMessage);
      return false;
    }
  }

  if (!googleUploadManifest(manifest, folderId, errorMessage)) {
    updateGoogleState("authorized", "upload_failed", errorMessage);
    return false;
  }

  g_googleDriveState.lastSyncAt = currentTimestamp();
  g_googleDriveState.lastSyncedManifestHash = manifest.manifestHash;
  g_googleDriveState.lastSyncedSnapshotAt = manifest.snapshotAt;
  g_googleDriveState.localSnapshotAt = manifest.snapshotAt;
  g_googleDriveState.syncStatus = "synced";
  g_googleDriveState.lastError = "";
  saveGoogleDriveState();
  appendDeviceLog("info", "google_sync_uploaded", "Uploaded backup snapshot to Google Drive.");
  return true;
}

bool googleRestoreBackupSnapshot(const BackupManifest& manifest, String& errorMessage) {
  if (!g_sdReady) {
    errorMessage = "Insert a replacement SD card and reboot to restore from Google Drive.";
    updateGoogleState("authorized", "sd_missing", errorMessage);
    return false;
  }

  String folderId;
  if (!googleResolveFolderId(folderId, errorMessage)) {
    return false;
  }

  g_googleDriveState.syncStatus = "restoring";
  saveGoogleDriveState();

  for (size_t i = 0; i < manifest.entries.size(); ++i) {
    const BackupFileEntry& entry = manifest.entries[i];
    String fileId;
    if (!googleFindFileInFolder(folderId, entry.remoteName, "", fileId, nullptr, errorMessage)) {
      updateGoogleState("authorized", "restore_failed", errorMessage);
      return false;
    }
    if (fileId.isEmpty()) {
      errorMessage = "Missing backup file on Google Drive: " + entry.remoteName;
      updateGoogleState("authorized", "restore_failed", errorMessage);
      return false;
    }
    if (!googleDownloadFileToStoragePath(fileId, entry.path, errorMessage)) {
      updateGoogleState("authorized", "restore_failed", errorMessage);
      return false;
    }
  }

  loadCloudBackupConfig();
  saveCloudBackupConfig();
  if (!loadInventory()) {
    errorMessage = "Google restore completed, but the restored inventory could not be loaded.";
    updateGoogleState("authorized", "restore_failed", errorMessage);
    return false;
  }

  g_googleDriveState.lastSyncAt = currentTimestamp();
  g_googleDriveState.lastSyncedManifestHash = manifest.manifestHash;
  g_googleDriveState.lastSyncedSnapshotAt = manifest.snapshotAt;
  g_googleDriveState.localSnapshotAt = manifest.snapshotAt;
  g_googleDriveState.syncStatus = "restored";
  g_googleDriveState.lastError = "";
  saveGoogleDriveState();
  appendDeviceLog("warn", "google_restore", "Restored inventory assets from Google Drive.");
  return true;
}

bool localBackupIsMissingCoreFiles() {
  if (!g_sdReady) {
    return true;
  }
  return !storageFs().exists(INVENTORY_FILE) ||
    !g_inventoryLoadHealthy ||
    !storageFs().exists(CLOUD_CONFIG_FILE) ||
    !storageFs().exists(UI_INDEX_FILE) ||
    !storageFs().exists(UI_ITEM_FILE);
}

bool googleReconcileBackup(bool forceUpload, bool forceRestore, String& errorMessage) {
  if (!googleCloudRequested()) {
    errorMessage = "Google backup is disabled.";
    return false;
  }
  if (!googleCredentialsConfigured()) {
    errorMessage = "Google client ID and secret are required.";
    updateGoogleState("credentials_missing", "idle", errorMessage);
    return false;
  }
  if (!googleAuthorized()) {
    errorMessage = "Link Google Drive first.";
    updateGoogleState("disconnected", "idle", errorMessage);
    return false;
  }

  BackupManifest remoteManifest;
  bool remoteFound = false;
  if (!googleDownloadManifest(remoteManifest, remoteFound, errorMessage)) {
    updateGoogleState("authorized", "sync_failed", errorMessage);
    return false;
  }

  BackupManifest localManifest;
  const bool localReady = buildLocalBackupManifest(localManifest);

  if (forceRestore) {
    if (!remoteFound) {
      errorMessage = "No Google Drive backup snapshot exists yet.";
      updateGoogleState("authorized", "restore_failed", errorMessage);
      return false;
    }
    return googleRestoreBackupSnapshot(remoteManifest, errorMessage);
  }

  if (forceUpload) {
    if (!localReady) {
      errorMessage = "There is no local SD snapshot to upload.";
      updateGoogleState("authorized", "upload_failed", errorMessage);
      return false;
    }
    return googleUploadBackupSnapshot(localManifest, errorMessage);
  }

  if (!remoteFound) {
    if (!localReady) {
      errorMessage = "Neither SD nor Google Drive has a backup snapshot yet.";
      updateGoogleState("authorized", "idle", errorMessage);
      return false;
    }
    return googleUploadBackupSnapshot(localManifest, errorMessage);
  }

  if (!localReady || localBackupIsMissingCoreFiles()) {
    return googleRestoreBackupSnapshot(remoteManifest, errorMessage);
  }

  if (localManifest.manifestHash == remoteManifest.manifestHash) {
    g_googleDriveState.lastSyncAt = currentTimestamp();
    g_googleDriveState.lastSyncedManifestHash = remoteManifest.manifestHash;
    g_googleDriveState.lastSyncedSnapshotAt = remoteManifest.snapshotAt;
    g_googleDriveState.localSnapshotAt = remoteManifest.snapshotAt;
    g_googleDriveState.syncStatus = "synced";
    g_googleDriveState.lastError = "";
    saveGoogleDriveState();
    return true;
  }

  const String lastSyncedHash = g_googleDriveState.lastSyncedManifestHash;
  if (!lastSyncedHash.isEmpty()) {
    if (localManifest.manifestHash == lastSyncedHash && remoteManifest.manifestHash != lastSyncedHash) {
      appendDeviceLog("warn", "google_conflict_remote", "Remote snapshot is newer than local; restoring remote copy.");
      return googleRestoreBackupSnapshot(remoteManifest, errorMessage);
    }
    if (remoteManifest.manifestHash == lastSyncedHash && localManifest.manifestHash != lastSyncedHash) {
      appendDeviceLog("info", "google_conflict_local", "Local snapshot is newer than remote; uploading SD copy.");
      return googleUploadBackupSnapshot(localManifest, errorMessage);
    }
  }

  if (timestampSortKey(remoteManifest.snapshotAt) > timestampSortKey(localManifest.snapshotAt)) {
    appendDeviceLog("warn", "google_conflict_resolved_remote", "Both copies changed; newer remote snapshot won.");
    return googleRestoreBackupSnapshot(remoteManifest, errorMessage);
  }

  appendDeviceLog("warn", "google_conflict_resolved_local", "Both copies changed; newer SD snapshot won.");
  return googleUploadBackupSnapshot(localManifest, errorMessage);
}

bool googleStartDeviceAuthorization(String& errorMessage) {
  if (!wifiConnectedForCloud()) {
    errorMessage = "Connect the ESP32 to WiFi before linking Google Drive.";
    return false;
  }
  if (!googleCredentialsConfigured()) {
    errorMessage = "Save a Google client ID and secret first.";
    return false;
  }

  String response;
  int statusCode = 0;
  const String body =
    "client_id=" + urlEncode(g_cloudBackupConfig.clientId) +
    "&scope=" + urlEncode(String(GOOGLE_DRIVE_SCOPE));
  if (!performHttpRequest("POST", GOOGLE_DEVICE_CODE_URL, "", "application/x-www-form-urlencoded", body, response, statusCode)) {
    errorMessage = "Failed to contact Google's device login endpoint.";
    return false;
  }
  if (statusCode < 200 || statusCode >= 300) {
    errorMessage = googleErrorMessageFromResponse(response);
    return false;
  }

  g_googleDriveState.deviceCode = jsonStringField(response, "device_code");
  g_googleDriveState.userCode = jsonStringField(response, "user_code");
  g_googleDriveState.verificationUrl = jsonStringField(response, "verification_url");
  if (g_googleDriveState.verificationUrl.isEmpty()) {
    g_googleDriveState.verificationUrl = jsonStringField(response, "verification_uri");
  }
  g_googleDriveState.devicePollIntervalSeconds = static_cast<uint32_t>(jsonLongField(response, "interval", 5));
  g_googleDriveState.authStatus = "pending";
  g_googleDriveState.lastError = "";
  saveGoogleDriveState();
  return !g_googleDriveState.deviceCode.isEmpty();
}

bool googlePollDeviceAuthorization(int& responseStatus, String& errorMessage) {
  responseStatus = 200;
  if (g_googleDriveState.deviceCode.isEmpty()) {
    errorMessage = "Start Google device login first.";
    return false;
  }

  String response;
  int statusCode = 0;
  const String body =
    "client_id=" + urlEncode(g_cloudBackupConfig.clientId) +
    "&client_secret=" + urlEncode(g_cloudBackupConfig.clientSecret) +
    "&device_code=" + urlEncode(g_googleDriveState.deviceCode) +
    "&grant_type=" + urlEncode("urn:ietf:params:oauth:grant-type:device_code");
  if (!performHttpRequest("POST", GOOGLE_TOKEN_URL, "", "application/x-www-form-urlencoded", body, response, statusCode)) {
    errorMessage = "Failed to poll Google's token endpoint.";
    return false;
  }

  if (statusCode < 200 || statusCode >= 300) {
    const String googleError = jsonStringField(response, "error");
    if (googleError == "authorization_pending") {
      g_googleDriveState.authStatus = "pending";
      g_googleDriveState.lastError = "";
      saveGoogleDriveState();
      responseStatus = 202;
      return false;
    }
    if (googleError == "slow_down") {
      g_googleDriveState.devicePollIntervalSeconds += 5;
      g_googleDriveState.authStatus = "pending";
      g_googleDriveState.lastError = "";
      saveGoogleDriveState();
      responseStatus = 202;
      return false;
    }

    errorMessage = googleErrorMessageFromResponse(response);
    updateGoogleState("error", "idle", errorMessage);
    clearPendingGoogleDeviceFlow();
    saveGoogleDriveState();
    return false;
  }

  const String refreshToken = jsonStringField(response, "refresh_token");
  if (!refreshToken.isEmpty()) {
    g_googleDriveState.refreshToken = refreshToken;
  }
  g_googleDriveState.accessToken = jsonStringField(response, "access_token");
  g_googleDriveState.tokenType = jsonStringField(response, "token_type");
  g_googleDriveState.scope = jsonStringField(response, "scope");
  g_googleDriveState.accessTokenExpiresAt = (millis() / 1000) + static_cast<uint32_t>(jsonLongField(response, "expires_in", 3600));
  if (g_googleDriveState.refreshToken.isEmpty()) {
    errorMessage = "Google did not return a refresh token. Revoke access and try again.";
    updateGoogleState("error", "idle", errorMessage);
    clearPendingGoogleDeviceFlow();
    saveGoogleDriveState();
    return false;
  }

  clearPendingGoogleDeviceFlow();
  g_googleDriveState.authStatus = "authorized";
  g_googleDriveState.syncStatus = g_googleDriveState.localSnapshotAt.isEmpty() ? "idle" : "local_dirty";
  g_googleDriveState.lastError = "";
  saveGoogleDriveState();
  return true;
}

void googleDisconnect() {
  g_googleDriveState = defaultGoogleDriveState();
  saveGoogleDriveState();
}

void maybeAutoSyncGoogle(const String& reason) {
  if (!googleCloudRequested() || !googleCredentialsConfigured() || !googleAuthorized() || !wifiConnectedForCloud()) {
    return;
  }

  String errorMessage;
  if (!googleReconcileBackup(false, false, errorMessage) && !errorMessage.isEmpty()) {
    appendDeviceLog("warn", "google_sync_skipped", reason + ": " + errorMessage);
  }
}

bool requireSdCard() {
  if (g_sdReady) {
    return true;
  }

  sendError(500, "SD card is not ready.");
  return false;
}

void updateWifiConnectionContext() {
  if (WiFi.status() != WL_CONNECTED) {
    g_timeSource = "uptime";
    return;
  }

  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const unsigned long syncStart = millis();
  while (!timeIsSynchronized() && millis() - syncStart < 5000UL) {
    delay(200);
  }
  g_timeSource = currentTimeSource();

  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    g_baseUrl = "http://" + String(HOSTNAME) + ".local";
    Serial.print("mDNS active: ");
    Serial.println(g_baseUrl);
  } else {
    g_baseUrl = "http://" + WiFi.localIP().toString();
    Serial.println("mDNS failed, using IP URL.");
  }
}

bool connectToWifiConfig(const WifiConfig& config, bool preserveAp, unsigned long timeoutMs, String& errorMessage) {
  if (!wifiConfigUsable(config)) {
    errorMessage = "WiFi SSID is missing.";
    g_wifiLastError = errorMessage;
    return false;
  }

  const wifi_mode_t targetMode = preserveAp && wifiApActive() ? WIFI_AP_STA : WIFI_STA;
  WiFi.mode(targetMode);
  delay(120);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());

  Serial.print("Connecting to WiFi");
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    Serial.print('.');
    delay(400);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    g_wifiLastError = "";
    updateWifiConnectionContext();
    return true;
  }

  errorMessage = "Failed to connect to \"" + config.ssid + "\" (" + wifiStatusLabel(WiFi.status()) + ").";
  g_wifiLastError = errorMessage;
  return false;
}

String wifiConfigToJson() {
  const WifiConfig activeConfig = effectiveWifiConfig();
  const bool connected = WiFi.status() == WL_CONNECTED;
  const bool apActive = wifiApActive();
  String payload = "{";
  payload += "\"config_source\":\"" + jsonEscape(wifiConfigSource()) + "\"";
  payload += ",\"saved_ssid\":\"" + jsonEscape(g_wifiConfig.ssid) + "\"";
  payload += ",\"saved_updated_at\":\"" + jsonEscape(g_wifiConfig.updatedAt) + "\"";
  payload += ",\"effective_ssid\":\"" + jsonEscape(activeConfig.ssid) + "\"";
  payload += ",\"connected\":" + String(connected ? "true" : "false");
  payload += ",\"current_ssid\":\"" + jsonEscape(connected ? WiFi.SSID() : "") + "\"";
  payload += ",\"current_ip\":\"" + jsonEscape(connected ? WiFi.localIP().toString() : "") + "\"";
  payload += ",\"current_rssi\":" + String(connected ? WiFi.RSSI() : 0);
  payload += ",\"wifi_status\":\"" + jsonEscape(wifiStatusLabel(WiFi.status())) + "\"";
  payload += ",\"wifi_mode\":\"" + jsonEscape(wifiModeLabel()) + "\"";
  payload += ",\"ap_active\":" + String(apActive ? "true" : "false");
  payload += ",\"ap_ssid\":\"" + jsonEscape(apActive ? String(WIFI_FALLBACK_AP_NAME) : "") + "\"";
  payload += ",\"ap_ip\":\"" + jsonEscape(apActive ? WiFi.softAPIP().toString() : "") + "\"";
  payload += ",\"last_error\":\"" + jsonEscape(g_wifiLastError) + "\"";
  payload += ",\"setup_required\":" + String(wifiCredentialsConfigured() ? "false" : "true");
  payload += "}";
  return payload;
}

void handleGetWifiConfig() {
  sendJson(200, wifiConfigToJson());
}

void handleWifiScan() {
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
    delay(120);
  } else if (mode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(120);
  }

  WiFi.scanDelete();
  const int count = WiFi.scanNetworks(false, true);

  struct ScannedNetwork {
    String ssid;
    int32_t rssi;
    wifi_auth_mode_t auth;
    int32_t channel;
  };

  std::vector<ScannedNetwork> networks;
  for (int i = 0; i < count; ++i) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.isEmpty()) {
      continue;
    }

    const int32_t rssi = WiFi.RSSI(i);
    int existingIndex = -1;
    for (size_t j = 0; j < networks.size(); ++j) {
      if (networks[j].ssid == ssid) {
        existingIndex = static_cast<int>(j);
        break;
      }
    }

    if (existingIndex >= 0 && rssi <= networks[existingIndex].rssi) {
      continue;
    }

    ScannedNetwork network;
    network.ssid = ssid;
    network.rssi = rssi;
    network.auth = WiFi.encryptionType(i);
    network.channel = WiFi.channel(i);

    if (existingIndex >= 0) {
      networks[existingIndex] = network;
    } else {
      networks.push_back(network);
    }
  }

  std::sort(networks.begin(), networks.end(), [](const ScannedNetwork& a, const ScannedNetwork& b) {
    return a.rssi > b.rssi;
  });

  String payload = "{";
  payload += "\"networks\":[";
  for (size_t i = 0; i < networks.size(); ++i) {
    if (i > 0) {
      payload += ',';
    }
    payload += "{";
    payload += "\"ssid\":\"" + jsonEscape(networks[i].ssid) + "\"";
    payload += ",\"rssi\":" + String(networks[i].rssi);
    payload += ",\"channel\":" + String(networks[i].channel);
    payload += ",\"secure\":" + String(networks[i].auth != WIFI_AUTH_OPEN ? "true" : "false");
    payload += ",\"auth\":\"" + jsonEscape(wifiEncryptionLabel(networks[i].auth)) + "\"";
    payload += "}";
  }
  payload += "]";
  payload += ",\"current_ssid\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "") + "\"";
  payload += "}";

  WiFi.scanDelete();
  sendJson(200, payload);
}

void handleSaveWifiConfig() {
  const String ssid = trimCopy(server.arg("ssid"));
  const String password = server.hasArg("password") ? server.arg("password") : "";

  if (ssid.isEmpty()) {
    sendError(400, "WiFi SSID is required.");
    return;
  }

  g_wifiConfig.ssid = ssid;
  g_wifiConfig.password = password;
  g_wifiConfig.updatedAt = currentTimestamp();
  if (!saveWifiConfig()) {
    sendError(500, "Failed to save WiFi settings.");
    return;
  }

  String errorMessage;
  const bool connected = connectToWifiConfig(g_wifiConfig, true, 20000UL, errorMessage);
  if (connected) {
    scheduleAccessPointShutdown(2500UL);
  } else if (!wifiApActive()) {
    startAccessPoint();
  }
  if (connected) {
    showBoardStatus("WIFI READY", currentNetworkDisplayLine(), "Saved network");
  } else {
    showBoardStatus("AP MODE", "WiFi connect failed", g_baseUrl);
  }

  sendJson(200, wifiConfigToJson());
}

void handleForgetWifiConfig() {
  clearWifiConfig();
  g_wifiLastError = "";
  if (WiFi.status() != WL_CONNECTED && !wifiApActive()) {
    startAccessPoint();
  }
  sendJson(200, wifiConfigToJson());
}

void sendSdUiUnavailable(const String& title, const String& message) {
  auto escapeForHtml = [](String value) -> String {
    value.replace("&", "&amp;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    value.replace("\"", "&quot;");
    value.replace("'", "&#39;");
    return value;
  };

  String html = "<!doctype html><html><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>";
  html += escapeForHtml(title);
  html += "</title><style>";
  html += "body{margin:0;font-family:Segoe UI,Tahoma,sans-serif;background:#eef2f4;color:#1b2f3f;padding:1rem;}";
  html += ".card{max-width:700px;margin:8vh auto 0;background:#fff;border:1px solid #d5dde2;border-radius:16px;padding:1rem 1.1rem;}";
  html += "h1{margin:0 0 .55rem;font-size:1.2rem;}p{margin:.45rem 0;line-height:1.5;color:#526777;word-break:break-word;}";
  html += "</style></head><body><main class=\"card\"><h1>";
  html += escapeForHtml(title);
  html += "</h1><p>";
  html += escapeForHtml(message);
  html += "</p><p>Insert a working SD card, then reboot the device.</p></main></body></html>";
  server.send(503, "text/html; charset=utf-8", html);
}

void handleIndexPage() {
  if (!g_sdReady) {
    sendSdUiUnavailable("SD Card Required", "The inventory UI is configured to run from SD storage only.");
    return;
  }

  if (streamHtmlFromSd(UI_INDEX_FILE)) {
    return;
  }

  if (syncUiAssetsToSd(true) && streamHtmlFromSd(UI_INDEX_FILE)) {
    return;
  }

  sendSdUiUnavailable("UI Assets Missing", "The SD card is mounted, but /ui/index.html could not be loaded.");
}

void handleItemPage() {
  if (!g_sdReady) {
    sendSdUiUnavailable("SD Card Required", "Item pages are configured to run from SD storage only.");
    return;
  }

  if (streamHtmlFromSd(UI_ITEM_FILE)) {
    return;
  }

  if (syncUiAssetsToSd(true) && streamHtmlFromSd(UI_ITEM_FILE)) {
    return;
  }

  sendSdUiUnavailable("UI Assets Missing", "The SD card is mounted, but /ui/item.html could not be loaded.");
}

void handleStatus() {
  g_timeSource = currentTimeSource();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const bool apActive = wifiApActive();
  String payload = "{";
  payload += "\"board\":\"" + jsonEscape(String(BOARD_NAME)) + "\"";
  payload += ",\"device_id\":\"" + jsonEscape(g_deviceId) + "\"";
  payload += ",\"storage_mode\":\"" + jsonEscape(String(STORAGE_MODE)) + "\"";
  payload += ",\"hostname\":\"" + jsonEscape(String(HOSTNAME)) + "\"";
  payload += ",\"base_url\":\"" + jsonEscape(g_baseUrl) + "\"";
  payload += ",\"brand_name\":\"" + jsonEscape(g_cloudBackupConfig.brandName) + "\"";
  payload += ",\"brand_logo_ref\":\"" + jsonEscape(g_cloudBackupConfig.brandLogoRef) + "\"";
  payload += ",\"backup_mode\":\"" + jsonEscape(g_cloudBackupConfig.backupMode) + "\"";
  payload += ",\"asset_mode\":\"" + jsonEscape(g_cloudBackupConfig.assetMode) + "\"";
  payload += ",\"auth_status\":\"" + jsonEscape(g_googleDriveState.authStatus) + "\"";
  payload += ",\"sync_status\":\"" + jsonEscape(g_googleDriveState.syncStatus) + "\"";
  payload += ",\"folder_id\":\"" + jsonEscape(g_googleDriveState.folderId) + "\"";
  payload += ",\"last_sync_at\":\"" + jsonEscape(g_googleDriveState.lastSyncAt) + "\"";
  payload += ",\"time_source\":\"" + jsonEscape(g_timeSource) + "\"";
  payload += ",\"wifi_connected\":" + String(wifiConnected ? "true" : "false");
  payload += ",\"wifi_ssid\":\"" + jsonEscape(wifiConnected ? WiFi.SSID() : "") + "\"";
  payload += ",\"wifi_ip\":\"" + jsonEscape(wifiConnected ? WiFi.localIP().toString() : "") + "\"";
  payload += ",\"wifi_rssi\":" + String(wifiConnected ? WiFi.RSSI() : 0);
  payload += ",\"wifi_mode\":\"" + jsonEscape(wifiModeLabel()) + "\"";
  payload += ",\"wifi_ap_active\":" + String(apActive ? "true" : "false");
  payload += ",\"wifi_ap_ssid\":\"" + jsonEscape(apActive ? String(WIFI_FALLBACK_AP_NAME) : "") + "\"";
  payload += ",\"wifi_config_source\":\"" + jsonEscape(wifiConfigSource()) + "\"";
  payload += ",\"wifi_saved_ssid\":\"" + jsonEscape(g_wifiConfig.ssid) + "\"";
  payload += ",\"wifi_setup_required\":" + String(wifiCredentialsConfigured() ? "false" : "true");
  payload += ",\"wifi_last_error\":\"" + jsonEscape(g_wifiLastError) + "\"";
  payload += ",\"sd_ready\":" + String(g_sdReady ? "true" : "false");
  payload += "}";
  sendJson(200, payload);
}

void handleDeviceLog() {
  if (!requireSdCard()) {
    return;
  }

  sendJson(200, tailLogJson(DEVICE_LOG_FILE));
}

void handleTimeLog() {
  sendJson(200, "{\"lines\":[]}");
}

String defaultOrdersJsonPayload() {
  return "{\"orders\":[]}";
}

String loadOrdersJsonPayload() {
  if (!g_sdReady) {
    return defaultOrdersJsonPayload();
  }

  ensureDataFiles();
  File f = storageFs().open(ORDERS_FILE, FILE_READ);
  if (!f) {
    return defaultOrdersJsonPayload();
  }

  String payload = f.readString();
  f.close();
  payload.trim();
  if (payload.isEmpty()) {
    return defaultOrdersJsonPayload();
  }

  return payload;
}

bool saveOrdersJsonPayload(const String& payload) {
  if (!g_sdReady) {
    return false;
  }

  String trimmed = trimCopy(payload);
  if (trimmed.isEmpty()) {
    return false;
  }

  File temp = storageFs().open(ORDERS_TMP_FILE, FILE_WRITE);
  if (!temp) {
    return false;
  }

  temp.print(trimmed);
  temp.close();

  storageFs().remove(ORDERS_FILE);
  if (!storageFs().rename(ORDERS_TMP_FILE, ORDERS_FILE)) {
    return false;
  }

  return true;
}

void handleGetOrders() {
  if (!requireSdCard()) {
    return;
  }

  sendJson(200, loadOrdersJsonPayload());
}

void handleSaveOrders() {
  if (!requireSdCard()) {
    return;
  }

  if (!server.hasArg("payload")) {
    sendError(400, "Missing orders payload.");
    return;
  }

  String payload = trimCopy(server.arg("payload"));
  String validationError;
  if (!validateOrdersPayload(payload, validationError)) {
    const bool tooLarge = validationError == "Orders payload is too large.";
    sendError(tooLarge ? 413 : 400, validationError);
    return;
  }

  if (!saveOrdersJsonPayload(payload)) {
    sendError(500, "Failed to save orders to SD card.");
    return;
  }

  markCloudDirty("orders_updated");
  appendDeviceLog("info", "orders_saved", "Order planner data saved to SD.");
  sendJson(200, "{\"ok\":true}");
}

void handleFulfillOrder() {
  if (!requireSdCard()) {
    return;
  }

  const String orderNumber = trimCopy(server.hasArg("order_number") ? sanitizeField(server.arg("order_number")) : "");
  if (orderNumber.isEmpty()) {
    sendError(400, "Missing order number.");
    return;
  }

  if (!server.hasArg("plan")) {
    sendError(400, "Missing fulfillment plan.");
    return;
  }

  std::vector<OrderFulfillmentEntry> requirements;
  String parseError;
  if (!parseOrderFulfillmentPlan(server.arg("plan"), requirements, parseError)) {
    sendError(400, parseError);
    return;
  }

  if (!server.hasArg("orders_payload")) {
    sendError(400, "Missing updated orders payload.");
    return;
  }

  String nextOrdersPayload = trimCopy(server.arg("orders_payload"));
  String validationError;
  if (!validateOrdersPayload(nextOrdersPayload, validationError)) {
    const bool tooLarge = validationError == "Orders payload is too large.";
    sendError(tooLarge ? 413 : 400, validationError);
    return;
  }

  struct InventoryDeduction {
    int itemIndex;
    String itemId;
    int32_t needed;
    int32_t beforeQty;
    int32_t afterQty;
  };

  std::vector<InventoryDeduction> deductions;
  deductions.reserve(requirements.size());

  for (size_t i = 0; i < requirements.size(); ++i) {
    const int idx = findItemIndex(requirements[i].itemId);
    if (idx < 0) {
      sendError(404, "Item not found for fulfillment: " + requirements[i].itemId);
      return;
    }

    const int32_t stock = g_items[idx].qty;
    if (requirements[i].needed > stock) {
      sendError(409, "Insufficient stock for " + g_items[idx].id + ". Needed " + String(requirements[i].needed) + ", available " + String(stock) + ".");
      return;
    }

    InventoryDeduction deduction;
    deduction.itemIndex = idx;
    deduction.itemId = g_items[idx].id;
    deduction.needed = requirements[i].needed;
    deduction.beforeQty = stock;
    deduction.afterQty = stock - requirements[i].needed;
    deductions.push_back(deduction);
  }

  if (deductions.empty()) {
    sendError(400, "No selected items to fulfill.");
    return;
  }

  const std::vector<ItemRecord> rollbackItems = g_items;
  const String updatedAt = currentTimestamp();
  int32_t totalRemoved = 0;
  for (size_t i = 0; i < deductions.size(); ++i) {
    g_items[deductions[i].itemIndex].qty = deductions[i].afterQty;
    g_items[deductions[i].itemIndex].updatedAt = updatedAt;
    totalRemoved += deductions[i].needed;
  }

  if (!saveInventory()) {
    g_items = rollbackItems;
    sendError(500, "Failed to save inventory while fulfilling the order.");
    return;
  }

  if (!saveOrdersJsonPayload(nextOrdersPayload)) {
    g_items = rollbackItems;
    saveInventory();
    sendError(500, "Failed to remove the fulfilled order.");
    return;
  }

  String detail = orderNumber + " ";
  for (size_t i = 0; i < deductions.size(); ++i) {
    if (i > 0) {
      detail += ", ";
    }
    detail += deductions[i].itemId + ":-" + String(deductions[i].needed);
  }
  if (detail.length() > 220) {
    detail = detail.substring(0, 217) + "...";
  }

  appendTransaction(orderNumber, "fulfill_order", -totalRemoved, 0, detail);
  appendDeviceLog("info", "order_fulfilled", detail);
  markCloudDirty("order_fulfilled");
  maybeAutoSyncGoogle("order_fulfilled");
  showBoardNotification("ORDER FULFILLED", orderNumber, String(totalRemoved) + " units removed", boardColor(223, 84, 79), 5500UL);

  String response = "{\"ok\":true";
  response += ",\"order_number\":\"" + jsonEscape(orderNumber) + "\"";
  response += ",\"line_count\":" + String(deductions.size());
  response += ",\"units_removed\":" + String(totalRemoved);
  response += "}";
  sendJson(200, response);
}

void handleGetCloudConfig() {
  loadCloudBackupConfig();
  loadGoogleDriveState();
  sendJson(200, cloudBackupConfigToJson());
}

void handleSaveCloudConfig() {
  const CloudBackupConfig previousConfig = g_cloudBackupConfig;
  CloudBackupConfig config = defaultCloudBackupConfig();
  config.provider = server.hasArg("provider") ? sanitizeField(server.arg("provider")) : "google_drive";
  config.loginEmail = server.hasArg("login_email") ? sanitizeField(server.arg("login_email")) : "";
  config.folderName = server.hasArg("folder_name") ? sanitizeField(server.arg("folder_name")) : "";
  config.folderHint = server.hasArg("folder_hint") ? sanitizeField(server.arg("folder_hint")) : "";
  config.mode = server.hasArg("mode") ? sanitizeField(server.arg("mode")) : "select_or_create";
  config.backupMode = server.hasArg("backup_mode") ? sanitizeField(server.arg("backup_mode")) : "sd_only";
  config.assetMode = server.hasArg("asset_mode") ? sanitizeField(server.arg("asset_mode")) : "sd_only";
  config.brandName = server.hasArg("brand_name") ? sanitizeField(server.arg("brand_name")) : "Stingray Inventory";
  config.brandLogoRef = server.hasArg("brand_logo_ref") ? sanitizeField(server.arg("brand_logo_ref")) : "";
  config.clientId = server.hasArg("client_id") ? sanitizeField(server.arg("client_id")) : "";
  config.clientSecret = server.hasArg("client_secret") ? sanitizeField(server.arg("client_secret")) : "";

  if (config.provider != "google_drive") {
    sendError(400, "Only Google Drive / Docs backup is supported in this setup path.");
    return;
  }

  if (config.mode != "select_or_create" && config.mode != "create" && config.mode != "use_existing") {
    sendError(400, "Invalid folder mode.");
    return;
  }

  if (config.backupMode != "sd_only" && config.backupMode != "hybrid_sd_google" && config.backupMode != "google_primary_sd_fallback") {
    sendError(400, "Invalid backup mode.");
    return;
  }

  if (config.assetMode != "sd_only" && config.assetMode != "sd_primary_google_backup" && config.assetMode != "google_primary_sd_fallback") {
    sendError(400, "Invalid asset mode.");
    return;
  }

  const bool cloudTargetNeeded =
    config.backupMode != "sd_only" ||
    config.assetMode != "sd_only";

  if (cloudTargetNeeded && config.folderName.isEmpty() && config.folderHint.isEmpty() && trimCopy(g_googleDriveState.folderId).isEmpty()) {
    sendError(400, "Folder name or folder hint is required.");
    return;
  }

  config.updatedAt = currentTimestamp();
  g_cloudBackupConfig = config;
  if (!saveCloudBackupConfig()) {
    sendError(500, "Failed to save cloud backup settings to device storage.");
    return;
  }

  const bool credentialsChanged =
    previousConfig.clientId != config.clientId ||
    previousConfig.clientSecret != config.clientSecret;
  if (credentialsChanged) {
    const String localSnapshotAt = !g_googleDriveState.localSnapshotAt.isEmpty()
      ? g_googleDriveState.localSnapshotAt
      : deriveLocalSnapshotAt();
    g_googleDriveState = defaultGoogleDriveState();
    g_googleDriveState.localSnapshotAt = localSnapshotAt;
  }
  if (!googleCredentialsConfigured() && googleCloudRequested()) {
    g_googleDriveState.authStatus = "credentials_missing";
  }
  saveGoogleDriveState();
  markCloudDirty("cloud_config_saved");
  appendDeviceLog("info", "config_saved", "Backup, branding, and cloud path settings updated.");
  maybeAutoSyncGoogle("cloud_config_saved");

  sendJson(200, cloudBackupConfigToJson());
}

void handleStartGoogleAuth() {
  String errorMessage;
  if (!googleStartDeviceAuthorization(errorMessage)) {
    sendError(400, errorMessage);
    return;
  }

  sendJson(200, cloudBackupConfigToJson());
}

void handlePollGoogleAuth() {
  int responseStatus = 200;
  String errorMessage;
  if (!googlePollDeviceAuthorization(responseStatus, errorMessage)) {
    if (responseStatus == 202) {
      sendJson(202, cloudBackupConfigToJson());
      return;
    }
    sendError(400, errorMessage);
    return;
  }

  maybeAutoSyncGoogle("google_authorized");
  sendJson(200, cloudBackupConfigToJson());
}

void handleDisconnectGoogleAuth() {
  googleDisconnect();
  sendJson(200, cloudBackupConfigToJson());
}

void handleGoogleSync() {
  String errorMessage;
  if (!googleReconcileBackup(false, false, errorMessage)) {
    sendError(500, errorMessage);
    return;
  }

  sendJson(200, cloudBackupConfigToJson());
}

void handleGoogleRestore() {
  String errorMessage;
  if (!googleReconcileBackup(false, true, errorMessage)) {
    sendError(500, errorMessage);
    return;
  }

  sendJson(200, cloudBackupConfigToJson());
}

void handleQrSvg() {
  if (!server.hasArg("data")) {
    server.send(400, "text/plain; charset=utf-8", "Missing QR data.");
    return;
  }

  String value = server.arg("data");
  value.trim();
  if (value.isEmpty()) {
    server.send(400, "text/plain; charset=utf-8", "Missing QR data.");
    return;
  }

  if (value.length() > 150) {
    server.send(413, "text/plain; charset=utf-8", "QR data is too long.");
    return;
  }

  String svg = qrCodeSvg(value);
  if (svg.isEmpty()) {
    server.send(500, "text/plain; charset=utf-8", "Failed to generate QR code.");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "image/svg+xml; charset=utf-8", svg);
}

void handleStoredFile() {
  if (!requireSdCard()) {
    return;
  }

  if (!server.hasArg("path")) {
    sendError(400, "Missing file path.");
    return;
  }

  String path = trimCopy(server.arg("path"));
  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  if (!isSafeStorageImagePath(path)) {
    sendError(403, "File path is not allowed.");
    return;
  }

  File f = storageFs().open(path, FILE_READ);
  if (!f) {
    sendError(404, "File not found.");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, contentTypeForPath(path));
  f.close();
}

void handleImageUploadData() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    g_uploadError = "";
    resetImageUploadState();

    if (!g_sdReady) {
      g_uploadError = "SD card is not ready.";
      return;
    }

    const String storagePath = uniqueImageStoragePath(upload.filename);
    if (storagePath.isEmpty()) {
      g_uploadError = "Unsupported image type. Use JPG, PNG, GIF, BMP, or WEBP.";
      return;
    }

    g_uploadFile = storageFs().open(storagePath, FILE_WRITE);
    if (!g_uploadFile) {
      g_uploadError = "Failed to create image file on SD card.";
      return;
    }

    g_uploadStoredPath = storagePath;
    g_uploadBytesWritten = 0;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!g_uploadError.isEmpty() || !g_uploadFile) {
      return;
    }

    if (g_uploadBytesWritten + upload.currentSize > MAX_IMAGE_UPLOAD_BYTES) {
      g_uploadError = "Image is too large. Limit is 2 MB.";
      resetImageUploadState();
      return;
    }

    if (g_uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      g_uploadError = "Failed to write image to SD card.";
      resetImageUploadState();
      return;
    }

    g_uploadBytesWritten += upload.currentSize;
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (g_uploadFile) {
      g_uploadFile.close();
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    g_uploadError = "Image upload was aborted.";
    resetImageUploadState();
  }
}

void handleImageUploadComplete() {
  if (!requireSdCard()) {
    resetImageUploadState();
    g_uploadError = "";
    return;
  }

  if (!g_uploadError.isEmpty()) {
    const String message = g_uploadError;
    resetImageUploadState();
    g_uploadError = "";
    sendError(400, message);
    return;
  }

  if (g_uploadStoredPath.isEmpty()) {
    sendError(400, "No image was uploaded.");
    return;
  }

  const String storagePath = g_uploadStoredPath;
  resetImageUploadState();
  g_uploadError = "";

  String payload = "{";
  payload += "\"ok\":true";
  payload += ",\"storage_path\":\"" + jsonEscape(storagePath) + "\"";
  payload += ",\"image_ref\":\"" + jsonEscape(imageRefFromStoragePath(storagePath)) + "\"";
  payload += "}";
  appendDeviceLog("info", "image_uploaded", "Stored image asset at " + storagePath);
  markCloudDirty("image_uploaded");
  maybeAutoSyncGoogle("image_uploaded");
  sendJson(201, payload);
}

void handleItemsList() {
  if (!requireSdCard()) {
    return;
  }

  const String categoryFilter = server.hasArg("category") ? server.arg("category") : "all";
  const String searchFilter = server.hasArg("q") ? server.arg("q") : "";

  String payload = "{\"items\":[";
  bool first = true;
  for (size_t i = 0; i < g_items.size(); ++i) {
    if (!matchesCategoryFilter(g_items[i], categoryFilter) || !matchesSearchFilter(g_items[i], searchFilter)) {
      continue;
    }

    if (!first) {
      payload += ',';
    }
    payload += itemToJson(g_items[i]);
    first = false;
  }
  payload += "]}";
  sendJson(200, payload);
}

void handleGetItem() {
  if (!requireSdCard()) {
    return;
  }

  String id;
  if (!parseIdArg("id", id)) {
    sendError(400, "Missing or invalid item id.");
    return;
  }

  const int idx = findItemIndex(id);
  if (idx < 0) {
    sendError(404, "Item not found.");
    return;
  }

  sendJson(200, itemPayloadJson(g_items[idx]));
}

void handleAddItem() {
  if (!requireSdCard()) {
    return;
  }

  const String category = server.hasArg("category") ? normalizeCategory(server.arg("category")) : String(DEFAULT_CATEGORY);
  String partName = server.hasArg("part_name") ? server.arg("part_name") : "";
  partName = sanitizeField(partName);
  if (partName.isEmpty()) {
    sendError(400, "Part name is required.");
    return;
  }

  String id = server.hasArg("id") ? sanitizeField(server.arg("id")) : "";
  id = trimCopy(id);
  if (id.isEmpty()) {
    sendError(400, "Part number is required.");
    return;
  }

  const String qrCode = server.hasArg("qr_code") ? sanitizeField(server.arg("qr_code")) : "";
  const String color = server.hasArg("color") ? sanitizeField(server.arg("color")) : "";
  const String material = server.hasArg("material") ? sanitizeField(server.arg("material")) : "";
  const String imageRef = server.hasArg("image_ref") ? sanitizeField(server.arg("image_ref")) : "";
  const String bomProduct = server.hasArg("bom_product") ? sanitizeField(server.arg("bom_product")) : "";

  int32_t bomQty = 0;
  if (server.hasArg("bom_qty") && !parseIntArg("bom_qty", bomQty)) {
    sendError(400, "BOM quantity must be an integer.");
    return;
  }

  if (bomQty < 0) {
    sendError(400, "BOM quantity cannot be negative.");
    return;
  }

  if (!bomProduct.isEmpty() && bomQty == 0) {
    bomQty = 1;
  }

  if (bomProduct.isEmpty() && bomQty > 0) {
    sendError(400, "BOM product is required when BOM quantity is set.");
    return;
  }

  if (category != "part" && (!bomProduct.isEmpty() || bomQty > 0)) {
    sendError(400, "Only parts can be assigned to a BOM product or kit.");
    return;
  }

  int32_t qty = 0;
  if (server.hasArg("qty") && !parseIntArg("qty", qty)) {
    sendError(400, "Quantity must be an integer.");
    return;
  }

  if (qty < 0) {
    sendError(400, "Quantity cannot be negative.");
    return;
  }

  if (findItemIndex(id) >= 0) {
    sendError(409, "Item id already exists.");
    return;
  }

  ItemRecord item;
  item.id = id;
  item.category = category;
  item.partName = partName;
  item.qrCode = qrCode;
  item.color = color;
  item.material = material;
  item.qty = qty;
  item.imageRef = imageRef;
  item.bomProduct = bomProduct;
  item.bomQty = bomQty;
  item.updatedAt = currentTimestamp();

  g_items.push_back(item);
  std::sort(g_items.begin(), g_items.end(), [](const ItemRecord& a, const ItemRecord& b) {
    return normalizeLookupValue(a.id) < normalizeLookupValue(b.id);
  });

  if (!saveInventory()) {
    const int rollbackIdx = findItemIndex(id);
    if (rollbackIdx >= 0) {
      g_items.erase(g_items.begin() + rollbackIdx);
    }
    sendError(500, "Failed to persist inventory to SD card.");
    return;
  }

  const int savedIdx = findItemIndex(id);
  if (savedIdx < 0) {
    sendError(500, "Item saved but cannot be reloaded.");
    return;
  }

  appendTransaction(item.id, "create", qty, qty, itemDisplayName(item) + " created");
  appendDeviceLog("info", "item_created", item.id + " saved with qty " + String(qty));
  markCloudDirty("item_created");
  maybeAutoSyncGoogle("item_created");
  notifyInventoryCreatedOnBoard(g_items[savedIdx]);
  sendJson(201, itemPayloadJson(g_items[savedIdx]));
}

void handleRemoveItem() {
  if (!requireSdCard()) {
    return;
  }

  String id;
  if (!parseIdArg("id", id)) {
    sendError(400, "Missing or invalid item id.");
    return;
  }

  const int idx = findItemIndex(id);
  if (idx < 0) {
    sendError(404, "Item not found.");
    return;
  }

  const ItemRecord removed = g_items[idx];
  g_items.erase(g_items.begin() + idx);

  if (!saveInventory()) {
    g_items.insert(g_items.begin() + idx, removed);
    sendError(500, "Failed to persist inventory to SD card.");
    return;
  }

  appendTransaction(id, "remove", -removed.qty, 0, "item removed");
  appendDeviceLog("info", "item_removed", id + " removed from inventory.");
  markCloudDirty("item_removed");
  maybeAutoSyncGoogle("item_removed");
  notifyInventoryRemovedOnBoard(removed);
  sendJson(200, "{\"ok\":true}");
}

void handleAdjustItem() {
  if (!requireSdCard()) {
    return;
  }

  String id;
  int32_t delta = 0;

  if (!parseIdArg("id", id)) {
    sendError(400, "Missing or invalid item id.");
    return;
  }

  if (!parseIntArg("delta", delta)) {
    sendError(400, "Missing or invalid delta.");
    return;
  }

  if (delta == 0) {
    sendError(400, "Delta cannot be zero.");
    return;
  }

  const int idx = findItemIndex(id);
  if (idx < 0) {
    sendError(404, "Item not found.");
    return;
  }

  const int32_t newQty = g_items[idx].qty + delta;
  if (newQty < 0) {
    sendError(400, "Quantity cannot go below zero.");
    return;
  }

  const int32_t previousQty = g_items[idx].qty;
  const String previousUpdatedAt = g_items[idx].updatedAt;
  g_items[idx].qty = newQty;
  g_items[idx].updatedAt = currentTimestamp();

  if (!saveInventory()) {
    g_items[idx].qty = previousQty;
    g_items[idx].updatedAt = previousUpdatedAt;
    sendError(500, "Failed to persist inventory to SD card.");
    return;
  }

  appendTransaction(id, "adjust", delta, newQty, "quantity updated from item page");
  appendDeviceLog("info", "item_adjusted", id + " adjusted by " + String(delta) + " to " + String(newQty));
  markCloudDirty("item_adjusted");
  maybeAutoSyncGoogle("item_adjusted");
  notifyInventoryAdjustedOnBoard(g_items[idx], delta);
  sendJson(200, itemPayloadJson(g_items[idx]));
}

void handleSetItemQty() {
  if (!requireSdCard()) {
    return;
  }

  String id;
  int32_t qty = 0;

  if (!parseIdArg("id", id)) {
    sendError(400, "Missing or invalid item id.");
    return;
  }

  if (!parseIntArg("qty", qty)) {
    sendError(400, "Missing or invalid quantity.");
    return;
  }

  if (qty < 0) {
    sendError(400, "Quantity cannot be negative.");
    return;
  }

  const int idx = findItemIndex(id);
  if (idx < 0) {
    sendError(404, "Item not found.");
    return;
  }

  const int32_t previousQty = g_items[idx].qty;
  const String previousUpdatedAt = g_items[idx].updatedAt;
  const int32_t delta = qty - previousQty;
  g_items[idx].qty = qty;
  g_items[idx].updatedAt = currentTimestamp();

  if (!saveInventory()) {
    g_items[idx].qty = previousQty;
    g_items[idx].updatedAt = previousUpdatedAt;
    sendError(500, "Failed to persist inventory to SD card.");
    return;
  }

  appendTransaction(id, "set_qty", delta, qty, "quantity set directly");
  appendDeviceLog("info", "item_set_qty", id + " quantity set to " + String(qty));
  markCloudDirty("item_set_qty");
  maybeAutoSyncGoogle("item_set_qty");
  notifyInventorySetQtyOnBoard(g_items[idx], previousQty);
  sendJson(200, itemPayloadJson(g_items[idx]));
}

void handleExportCsv() {
  if (!requireSdCard()) {
    return;
  }

  const String categoryFilter = server.hasArg("category") ? server.arg("category") : "all";
  String csv = "part_number,category,part_name,qr_code,color,material,qty,image_ref,bom_product,bom_qty,updated_at,qr_link\n";
  for (const ItemRecord& item : g_items) {
    if (!matchesCategoryFilter(item, categoryFilter)) {
      continue;
    }

    csv += csvEscape(item.id) + ',';
    csv += csvEscape(normalizeCategory(item.category)) + ',';
    csv += csvEscape(item.partName) + ',';
    csv += csvEscape(item.qrCode) + ',';
    csv += csvEscape(item.color) + ',';
    csv += csvEscape(item.material) + ',';
    csv += String(item.qty) + ',';
    csv += csvEscape(item.imageRef) + ',';
    csv += csvEscape(item.bomProduct) + ',';
    csv += String(item.bomQty) + ',';
    csv += csvEscape(item.updatedAt) + ',';
    csv += csvEscape(itemUrl(item.id));
    csv += '\n';
  }

  String fileName = "inventory_export";
  String normalizedFilter = trimCopy(categoryFilter);
  normalizedFilter.toLowerCase();
  if (!normalizedFilter.isEmpty() && normalizedFilter != "all") {
    fileName += "_";
    fileName += normalizedFilter;
  }
  fileName += ".csv";

  server.sendHeader("Content-Disposition", String("attachment; filename=") + fileName);
  server.send(200, "text/csv; charset=utf-8", csv);
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    sendError(404, "API route not found.");
  } else {
    server.send(404, "text/plain; charset=utf-8", "Not found");
  }
}

void connectWifi() {
  const WifiConfig config = effectiveWifiConfig();
  if (!wifiCredentialsConfigured()) {
    Serial.println("WiFi credentials not configured. Starting AP mode immediately.");
    startAccessPoint();
    showBoardStatus("AP MODE", "WiFi creds missing", g_baseUrl);
    appendDeviceLog("warn", "wifi_credentials_missing", "WiFi credentials not configured; AP mode started.");
    appendTimeLog("clock_offline", "Using uptime-based timestamps because WiFi credentials are missing.");
    return;
  }

  String errorMessage;
  if (connectToWifiConfig(config, false, 30000UL, errorMessage)) {
    appendDeviceLog("info", "wifi_connected", "Connected to WiFi at " + g_baseUrl);
    appendTimeLog(g_timeSource == "ntp" ? "clock_synced" : "clock_pending", g_timeSource == "ntp" ? "Clock synchronized from NTP." : "WiFi connected, but the clock is still using uptime fallback.");
    showBoardStatus("WIFI READY", currentNetworkDisplayLine(), g_timeSource == "ntp" ? "Clock synced" : "Clock pending");
    return;
  }

  Serial.println("WiFi failed; switching to AP fallback.");
  startAccessPoint();
  showBoardStatus("AP MODE", "WiFi failed", g_baseUrl);
  appendDeviceLog("warn", "wifi_failed", "WiFi connection failed; AP fallback started.");
  appendTimeLog("clock_offline", "Using uptime-based timestamps because WiFi did not connect.");
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleIndexPage);
  server.on("/settings", HTTP_GET, handleIndexPage);
  server.on("/orders", HTTP_GET, handleIndexPage);
  server.on("/orders/view", HTTP_GET, handleIndexPage);
  server.on("/orders/fulfill", HTTP_GET, handleIndexPage);
  server.on("/item", HTTP_GET, handleItemPage);
  server.on("/qr.svg", HTTP_GET, handleQrSvg);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi/config", HTTP_GET, handleGetWifiConfig);
  server.on("/api/wifi/config", HTTP_POST, handleSaveWifiConfig);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/forget", HTTP_POST, handleForgetWifiConfig);
  server.on("/api/cloud-config", HTTP_GET, handleGetCloudConfig);
  server.on("/api/cloud-config", HTTP_POST, handleSaveCloudConfig);
  server.on("/api/google-auth/start", HTTP_POST, handleStartGoogleAuth);
  server.on("/api/google-auth/poll", HTTP_POST, handlePollGoogleAuth);
  server.on("/api/google-auth/disconnect", HTTP_POST, handleDisconnectGoogleAuth);
  server.on("/api/google-drive/sync", HTTP_POST, handleGoogleSync);
  server.on("/api/google-drive/restore", HTTP_POST, handleGoogleRestore);
  server.on("/api/logs/device", HTTP_GET, handleDeviceLog);
  server.on("/api/logs/time", HTTP_GET, handleTimeLog);
  server.on("/api/orders", HTTP_GET, handleGetOrders);
  server.on("/api/orders", HTTP_POST, handleSaveOrders);
  server.on("/api/orders/fulfill", HTTP_POST, handleFulfillOrder);
  server.on("/api/files", HTTP_GET, handleStoredFile);
  server.on("/api/images/upload", HTTP_POST, handleImageUploadComplete, handleImageUploadData);
  server.on("/api/items", HTTP_GET, handleItemsList);
  server.on("/api/item", HTTP_GET, handleGetItem);
  server.on("/api/items/add", HTTP_POST, handleAddItem);
  server.on("/api/items/remove", HTTP_POST, handleRemoveItem);
  server.on("/api/items/adjust", HTTP_POST, handleAdjustItem);
  server.on("/api/items/set", HTTP_POST, handleSetItemQty);
  server.on("/api/export", HTTP_GET, handleExportCsv);

  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204, "text/plain", "");
  });

  server.onNotFound(handleNotFound);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  initBoardFeedback();
  showBoardStatus("BOOTING", BOARD_NAME, "Starting firmware");
  g_deviceId = deviceId();
  g_macAddress = macAddressString();
  g_resetReason = resetReasonString(esp_reset_reason());
  g_cloudBackupConfig = defaultCloudBackupConfig();
  g_googleDriveState = defaultGoogleDriveState();
  g_wifiConfig = defaultWifiConfig();
  g_preferencesReady = g_preferences.begin(PREFS_NAMESPACE, false);
  loadWifiConfig();

  if (!loadCloudBackupConfig()) {
    g_cloudBackupConfig = defaultCloudBackupConfig();
  }
  if (!loadGoogleDriveState()) {
    g_googleDriveState = defaultGoogleDriveState();
  }

  g_sdReady = initStorage();
  if (g_sdReady) {
    Serial.println("SD card initialized.");
    showBoardStatus("SD READY", STORAGE_MODE, "Storage online");
    if (g_resetReason != "power_on" && g_resetReason != "unknown") {
      appendDeviceLog("info", "reset", g_resetReason);
    }
    appendDeviceLog("info", "boot", "Device boot started.");
    appendDeviceLog("info", "sd_ready", "SD card initialized successfully.");
    saveCloudBackupConfig();
    saveGoogleDriveState();
    if (!syncUiAssetsToSd()) {
      Serial.println("Warning: could not sync UI assets to SD.");
      appendDeviceLog("error", "ui_sync_failed", "Could not sync UI assets to SD.");
    } else {
      appendDeviceLog("info", "ui_synced", "UI assets synchronized to SD.");
    }
    if (!loadInventory()) {
      Serial.println("Warning: could not load inventory from SD.");
      appendDeviceLog("error", "inventory_load_failed", "Could not load inventory from SD.");
    }
    if (!loadCloudBackupConfig()) {
      Serial.println("Warning: could not load cloud backup config from SD.");
      appendDeviceLog("error", "config_load_failed", "Could not load system backup config from SD.");
    }
    loadGoogleDriveState();
  } else {
    Serial.println("Warning: SD card init failed.");
    showBoardStatus("SD MISSING", "Insert SD card", "Cloud restore waits");
    if (googleAuthorized()) {
      updateGoogleState("authorized", "sd_missing", "Insert a replacement SD card and reboot to restore from Google Drive.");
    }
  }

  enforceStandaloneSdBaseline();
  connectWifi();
  if (googleCloudRequested() && googleCredentialsConfigured() && googleAuthorized() && wifiConnectedForCloud()) {
    String errorMessage;
    if (!googleReconcileBackup(false, false, errorMessage) && !errorMessage.isEmpty()) {
      appendDeviceLog("warn", "google_boot_sync_failed", errorMessage);
    }
  }
  setupRoutes();

  server.begin();
  Serial.println("HTTP server started.");
  showBoardStatus("SERVER READY", currentNetworkDisplayLine(), "Items " + String(g_items.size()));
}

void loop() {
  server.handleClient();
  processPendingAccessPointShutdown();
  updateBoardFeedback();
  delay(2);
}
