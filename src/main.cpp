#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PNGdec.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "logging.h"
#include "gerty_protocol.h"
#include "epd_driver.h"
#include "i2s_data_bus.h"
#include "firasans.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

RTC_DATA_ATTR uint8_t lastIdentity[32] = {};
RTC_DATA_ATTR bool hasImage = false;
RTC_DATA_ATTR uint32_t refreshSeconds = Config::DEFAULT_REFRESH_SECONDS;
RTC_DATA_ATTR uint32_t failures = 0;
uint32_t requestedPage = 0;
uint32_t savedPageCount = 0;
Preferences pagePreferences;
struct SavedPages {
  uint8_t endpointHash[32];
  uint32_t nextPage;
  uint32_t pageCount;
};
SavedPages savedPages = {};
bool pageStorageReady = false;
bool pageStateDirty = true;

void loadPages() {
  uint8_t endpointHash[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(Config::MANIFEST_URL),
                     strlen(Config::MANIFEST_URL), endpointHash, 0);
  pageStorageReady = pagePreferences.begin("gerty-pages", false);
  if (pageStorageReady && pagePreferences.getBytesLength("state") == sizeof(savedPages)) {
    pagePreferences.getBytes("state", &savedPages, sizeof(savedPages));
    if (memcmp(savedPages.endpointHash, endpointHash, 32) == 0 &&
        Gerty::validPages(savedPages.nextPage, savedPages.pageCount, savedPages.nextPage)) {
      requestedPage = savedPages.nextPage;
      savedPageCount = savedPages.pageCount;
      pageStateDirty = false;
    }
  }
  memcpy(savedPages.endpointHash, endpointHash, 32);
  if (!pageStorageReady) LOG_ERROR("Cannot open persistent page storage");
  LOG_INFO("Saved pagination: next_page=%u page_count=%u", requestedPage, savedPageCount);
}

void savePages(uint32_t nextPage, uint32_t pageCount) {
  requestedPage = nextPage;
  savedPageCount = pageCount;
  if (pageStorageReady &&
      (pageStateDirty || savedPages.nextPage != nextPage || savedPages.pageCount != pageCount ||
       pagePreferences.getBytesLength("state") != sizeof(savedPages))) {
    SavedPages state = savedPages;
    state.nextPage = nextPage;
    state.pageCount = pageCount;
    if (pagePreferences.putBytes("state", &state, sizeof(state)) == sizeof(state)) {
      savedPages = state;
      pageStateDirty = false;
    } else LOG_ERROR("Could not persist pagination");
  }
  LOG_INFO("Next request: page=%u of %u", requestedPage, savedPageCount);
}
RTC_DATA_ATTR char shownError[64] = {};
RTC_DATA_ATTR int32_t errorWidth = 0;
RTC_DATA_ATTR int32_t errorHeight = 0;
String updateError;
PNG png;
uint8_t *framebuffer = nullptr;
int decodedRows = 0;

bool fail(const String &reason) {
  updateError = reason;
  return false;
}

bool finishDisplay() {
  // The draw call joins its rendering tasks. Also drain the last bus transfer
  // before removing panel power; a timeout must not count as a displayed page.
  const uint32_t started = millis();
  while (i2s_is_busy() && millis() - started < 1000) delay(1);
  const bool idle = !i2s_is_busy();
  LOG_INFO("Display output %s; sequencing power off", idle ? "idle" : "timed out");
  // Unlike epd_poweroff_all(), this disables the positive and negative rails
  // in sequence with the driver's delays and leaves power_disable asserted.
  epd_poweroff();
  LOG_INFO("Display power-off complete");
  if (!idle) return fail("Display output timeout");
  return true;
}

void displayStage(const char *message) {
  LOG_INFO("%s", message);
  if (Config::LOG_LEVEL != Config::LogLevel::NONE) Serial.flush();
}

void showError() {
  if (updateError.isEmpty()) updateError = "Update failed";
  LOG_ERROR("%s", updateError.c_str());
  // An error covers image pixels; a later success must restore the full image,
  // even if the server still advertises the same revision.
  hasImage = false;
  if (updateError == shownError) return;
  int32_t x = 0, y = 0, left, top, width, height;
  get_text_bounds(&FiraSans, updateError.c_str(), &x, &y,
                  &left, &top, &width, &height, nullptr);
  int32_t panelWidth = ((width + 24 + 3) / 4) * 4;
  int32_t panelHeight = height + 24;
  // Include the old message's area when replacing it with a shorter one.
  errorWidth = min(int32_t(EPD_WIDTH), max(errorWidth, panelWidth));
  errorHeight = min(int32_t(EPD_HEIGHT), max(errorHeight, panelHeight));
  Rect_t area = {EPD_WIDTH - errorWidth, EPD_HEIGHT - errorHeight,
                 errorWidth, errorHeight};
  x = EPD_WIDTH - 12 - width - left;
  // The driver's direct-text path places its bitmap at y - height - top.
  y = EPD_HEIGHT - 12 + top;
  epd_poweron();
  epd_clear_area(area);
  writeln(&FiraSans, updateError.c_str(), &x, &y, nullptr);
  if (finishDisplay()) strlcpy(shownError, updateError.c_str(), sizeof(shownError));
}

// HTTPClient handles Content-Length and chunked transfer encoding. This sink
// bounds both RAM use and the total time spent accepting the response body.
class BoundedBuffer : public Stream {
 public:
  uint8_t *data;
  size_t used = 0;
  const size_t capacity;
  const uint32_t started = millis();
  explicit BoundedBuffer(size_t cap) : capacity(cap) {
    data = static_cast<uint8_t *>(ps_malloc(cap));
  }
  ~BoundedBuffer() { free(data); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *p, size_t n) override {
    if (!data || n > capacity - used ||
        millis() - started > Config::DOWNLOAD_TIMEOUT_MS) return 0;
    memcpy(data + used, p, n);
    used += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

bool fetch(const String &url, BoundedBuffer &body) {
  LOG_INFO("GET: %s", url.c_str());
  if (!Gerty::isWebUrl(url.c_str())) return fail("HTTP(S) URL required");
  if (!body.data) return fail("Not enough memory");
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // HTTPS encryption without certificate verification.
  secureClient.setHandshakeTimeout(15);
  WiFiClient &client = url.startsWith("https://")
      ? static_cast<WiFiClient &>(secureClient) : plainClient;
  HTTPClient http;
  http.setConnectTimeout(Config::HTTP_TIMEOUT_MS);
  http.setTimeout(Config::HTTP_TIMEOUT_MS);
  // Endpoints must be direct URLs; redirects remain disabled.
  if (!http.begin(client, url)) return fail("Cannot open URL");
  int code = http.GET();
  const bool tooLarge = http.getSize() > static_cast<int>(body.capacity);
  bool ok = false;
  if (code == HTTP_CODE_OK && http.getSize() <= static_cast<int>(body.capacity)) {
    int received = http.writeToStream(&body);
    ok = received > 0 && static_cast<size_t>(received) == body.used;
  }
  LOG_INFO("GET status=%d bytes=%u success=%d", code, body.used, ok);
  http.end();
  if (!ok) {
    if (code < 0) return fail("Server unavailable");
    if (code != HTTP_CODE_OK) return fail("HTTP " + String(code));
    if (tooLarge || body.used == body.capacity) return fail("Download too large");
    return fail("Download incomplete");
  }
  return ok;
}

int drawLine(PNGDRAW *line) {
  if (line->iWidth != EPD_WIDTH || line->y != decodedRows ||
      line->y >= EPD_HEIGHT) return 0;
  uint16_t pixels[EPD_WIDTH];
  png.getLineAsRGB565(line, pixels, PNG_RGB565_LITTLE_ENDIAN, 0x00FFFFFF);
  static const int bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int x = 0; x < EPD_WIDTH; ++x) {
    uint16_t p = pixels[x];
    int r = ((p >> 11) & 31) * 255 / 31;
    int g = ((p >> 5) & 63) * 255 / 63;
    int b = (p & 31) * 255 / 31;
    int gray = (77 * r + 150 * g + 29 * b + 128) >> 8;
    if (Config::DITHER && gray > 0 && gray < 255)
      gray += bayer[line->y & 3][x & 3] - 8;
    int level = constrain((gray + 8) / 17, 0, 15);
    epd_draw_pixel(x, line->y, level * 17, framebuffer);
  }
  ++decodedRows;
  return 1;
}

bool updateImage() {
  BoundedBuffer manifest(Config::MAX_JSON_BYTES);
  if (savedPageCount == 0 || requestedPage >= savedPageCount) requestedPage = 0;
  LOG_INFO("Requesting Gerty page=%u page_count=%u", requestedPage, savedPageCount);
  String manifestUrl = Gerty::pageUrl(Config::MANIFEST_URL, requestedPage).c_str();
  if (!fetch(manifestUrl, manifest)) {
    // The page list may have shrunk since the previous wake.
    if (updateError == "HTTP 404" && requestedPage != 0) savePages(0, 0);
    updateError = "JSON: " + updateError;
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, manifest.data, manifest.used)) return fail("Invalid JSON");
  if (!doc["schema_version"].is<int>() || doc["schema_version"].as<int>() != 1 ||
      !doc["refresh_seconds"].is<uint32_t>() ||
      !doc["image_url"].is<const char *>() ||
      !doc["image_revision"].is<const char *>()) return fail("Invalid JSON fields");
  uint32_t nextPage = 0;
  uint32_t pageCount = 1;
  const bool paginated = !doc["page"].isNull() || !doc["page_count"].isNull() ||
                         !doc["next_page"].isNull();
  if (paginated) {
    if (!doc["page"].is<uint32_t>() || !doc["page_count"].is<uint32_t>() ||
        !doc["next_page"].is<uint32_t>() ||
        !Gerty::validPages(doc["page"].as<uint32_t>(), doc["page_count"].as<uint32_t>(),
                          doc["next_page"].as<uint32_t>())) return fail("Invalid page metadata");
    nextPage = doc["next_page"].as<uint32_t>();
    pageCount = doc["page_count"].as<uint32_t>();
    LOG_INFO("Gerty page=%u count=%u next=%u", doc["page"].as<uint32_t>(),
             doc["page_count"].as<uint32_t>(), nextPage);
  }
  String url = doc["image_url"].as<String>();
  String revision = doc["image_revision"].as<String>();
  if (!Gerty::isWebUrl(url.c_str()) || url.length() > 2048 ||
      revision.isEmpty() || revision.length() > 256) return fail("Invalid image URL/revision");
  refreshSeconds = constrain(doc["refresh_seconds"].as<uint32_t>(),
                             Config::MIN_REFRESH_SECONDS, Config::MAX_REFRESH_SECONDS);
  String identity = url + "\n" + revision;
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(identity.c_str()),
                     identity.length(), digest, 0);
  if (hasImage && memcmp(digest, lastIdentity, sizeof(digest)) == 0) {
    LOG_INFO("Image unchanged");
    savePages(nextPage, pageCount);
    return true;
  }
  BoundedBuffer image(Config::MAX_PNG_BYTES);
  if (!fetch(url, image)) {
    updateError = "Image: " + updateError;
    return false;
  }
  int openResult = png.openRAM(image.data, image.used, drawLine);
  if (openResult != PNG_SUCCESS) {
    LOG_ERROR("PNG open failed: code=%d", openResult);
    return fail("PNG open error " + String(openResult));
  }
  LOG_DEBUG("PNG: %dx%d depth=%d type=%d interlaced=%d",
                png.getWidth(), png.getHeight(), png.getBpp(),
                png.getPixelType(), png.isInterlaced());
  // Restrict 16-bit-per-channel files as well as interlaced files in this POC.
  if (png.getWidth() != EPD_WIDTH || png.getHeight() != EPD_HEIGHT ||
      png.isInterlaced() || png.getBpp() > 8) {
    LOG_ERROR("PNG format rejected: requires 960x540, non-interlaced, <=8 bits/channel");
    png.close();
    return fail("PNG format unsupported");
  }
  framebuffer = static_cast<uint8_t *>(ps_malloc(EPD_WIDTH * EPD_HEIGHT / 2));
  if (!framebuffer) {
    LOG_ERROR("PNG framebuffer allocation failed");
    png.close();
    return fail("Not enough image memory");
  }
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
  decodedRows = 0;
  int result = png.decode(nullptr, PNG_CHECK_CRC);
  png.close();
  bool ok = result == PNG_SUCCESS && decodedRows == EPD_HEIGHT;
  if (!ok) fail("PNG decode error " + String(result));
  LOG_DEBUG("PNG decode: code=%d rows=%d/%d", result, decodedRows, EPD_HEIGHT);
  if (ok) {
    displayStage("Display power-on starting");
    epd_poweron();
    displayStage("Display clear starting");
    epd_clear();
    displayStage("Display clear complete; grayscale draw starting");
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    displayStage("Display grayscale draw returned");
    ok = finishDisplay();
  }
  if (ok) {
    memcpy(lastIdentity, digest, sizeof(digest));
    hasImage = true;
    savePages(nextPage, pageCount);
    shownError[0] = '\0';
    errorWidth = errorHeight = 0;
    LOG_INFO("Display refresh completed; page saved");
  }
  free(framebuffer);
  framebuffer = nullptr;
  return ok;
}

void updateCycle() {
  updateError = "";
  bool ok = false;
  if (psramFound()) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    LOG_INFO("Connecting to Wi-Fi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - started < Config::WIFI_TIMEOUT_MS) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      LOG_INFO("Wi-Fi connected; IP: %s", WiFi.localIP().toString().c_str());
      ok = updateImage();
    } else {
      LOG_ERROR("Wi-Fi connection failed; status=%d", WiFi.status());
      fail("Wi-Fi unavailable");
    }
  } else fail("PSRAM unavailable");
  uint32_t sleepSeconds = refreshSeconds;
  if (ok) failures = 0;
  else {
    failures = min(failures + 1, uint32_t(5));
    sleepSeconds = min(uint32_t(30) << (failures - 1), uint32_t(300));
    LOG_ERROR("Update failed; keeping previous image");
    showError();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  epd_poweroff();
  LOG_INFO("Sleeping %u seconds", sleepSeconds);
  if (Config::LOG_LEVEL != Config::LogLevel::NONE) Serial.flush();
  esp_sleep_enable_timer_wakeup(uint64_t(sleepSeconds) * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  if (Config::LOG_LEVEL != Config::LogLevel::NONE) {
    Serial.begin(115200);
    // Allow USB to attach on reset, without delaying normal timer wakes.
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
      uint32_t started = millis();
      while (!Serial && millis() - started < 1500) delay(50);
    }
  }
  LOG_INFO("Gerty boot; reset=%d; PSRAM=%u bytes", esp_reset_reason(), ESP.getPsramSize());
  LOG_DEBUG("Initializing display driver...");
  epd_init();
  epd_poweroff();
  LOG_DEBUG("Display driver initialized");
  // A reset/upload forces a redraw, while timer wakes retain the revision.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    hasImage = false;
    refreshSeconds = Config::DEFAULT_REFRESH_SECONDS;
    failures = 0;
  }
  loadPages();
}

void loop() { updateCycle(); }
