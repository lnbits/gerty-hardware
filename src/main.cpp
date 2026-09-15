#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PNGdec.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR >= 3
#define mbedtls_sha256_ret mbedtls_sha256
#endif
#ifdef GERTY_WAVESHARE_C6
#include <LittleFS.h>
static File pngFile;
static void *openPngFile(const char *name, int32_t *size) {
  pngFile = LittleFS.open(name, "r");
  if (!pngFile) return nullptr;
  *size = pngFile.size();
  return &pngFile;
}
static void closePngFile(void *) { pngFile.close(); }
static int32_t readPngFile(PNGFILE *file, uint8_t *buffer, int32_t length) {
  int32_t received = pngFile.read(buffer, length);
  file->iPos = pngFile.position();
  return received;
}
static int32_t seekPngFile(PNGFILE *file, int32_t position) {
  if (position < 0 || !pngFile.seek(position)) return -1;
  file->iPos = position;
  return position;
}
#endif

static void *imageAlloc(size_t size) {
#ifdef GERTY_WAVESHARE_C6
  return malloc(size);
#else
  return ps_malloc(size);
#endif
}
#include "config.h"
#include "logging.h"
#include "gerty_protocol.h"
#include "display.h"
#if defined(GERTY_RELEASE)
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
#elif __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#include "provisioning.h"

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
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(Provisioning::endpoint.c_str()),
                     Provisioning::endpoint.length(), endpointHash, 0);
  pageStorageReady = pagePreferences.begin("gerty-pages", false);
  if (pageStorageReady && pagePreferences.getBytesLength("state") == sizeof(savedPages)) {
    pagePreferences.getBytes("state", &savedPages, sizeof(savedPages));
    if (memcmp(savedPages.endpointHash, endpointHash, 32) == 0 &&
        (savedPages.pageCount == 0 ||
         Gerty::validPages(savedPages.nextPage, savedPages.pageCount, savedPages.nextPage))) {
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
String updateError;
PNG png;
uint8_t *framebuffer = nullptr;
int decodedRows = 0;
bool displayReady = false;

bool fail(const String &reason) {
  updateError = reason;
  return false;
}

void showError() {
  if (updateError.isEmpty()) updateError = "Update failed";
  LOG_ERROR("%s", updateError.c_str());
  hasImage = false;
  if (updateError == shownError) return;
  if (Display::showError(updateError.c_str()))
    strlcpy(shownError, updateError.c_str(), sizeof(shownError));
}

// HTTPClient handles Content-Length and chunked transfer encoding. This sink
// bounds both RAM use and the total time spent accepting the response body.
class BoundedBuffer : public Stream {
 public:
  uint8_t *data;
  size_t used = 0;
  const size_t capacity;
  const uint32_t started = millis();
  explicit BoundedBuffer(size_t cap, bool image = false) : capacity(cap) {
#ifdef GERTY_WAVESHARE_C6
    if (image) {
      disk = true;
      if (LittleFS.begin(true)) file = LittleFS.open("/download.png", "w");
      data = nullptr;
      return;
    }
#endif
    data = static_cast<uint8_t *>(imageAlloc(cap));
  }
  ~BoundedBuffer() {
    free(data);
#ifdef GERTY_WAVESHARE_C6
    if (disk) { file.close(); LittleFS.remove("/download.png"); }
#endif
  }
  bool ready() const {
#ifdef GERTY_WAVESHARE_C6
    if (disk) return bool(file);
#endif
    return data != nullptr;
  }
#ifdef GERTY_WAVESHARE_C6
  File file;
  bool disk = false;
#endif
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *p, size_t n) override {
    if (!ready() || n > capacity - used ||
        millis() - started > Config::DOWNLOAD_TIMEOUT_MS) return 0;
#ifdef GERTY_WAVESHARE_C6
    if (disk) {
      size_t written = file.write(p, n);
      used += written;
      return written;
    }
#endif
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
  if (!body.ready()) return fail("Download storage unavailable");
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
  if (line->iWidth != Display::WIDTH || line->y != decodedRows ||
      line->y >= Display::HEIGHT) return 0;
  uint16_t pixels[Display::WIDTH];
  png.getLineAsRGB565(line, pixels, PNG_RGB565_LITTLE_ENDIAN, 0x00FFFFFF);
  Display::writeRow(framebuffer, line->y, pixels);
  ++decodedRows;
  return 1;
}

bool updateImage() {
  BoundedBuffer manifest(Config::MAX_JSON_BYTES);
  if (savedPageCount > 0 && requestedPage >= savedPageCount) requestedPage = 0;
  LOG_INFO("Requesting Gerty page=%u page_count=%u", requestedPage, savedPageCount);
  String manifestUrl = Gerty::pageUrl(Provisioning::endpoint.c_str(), requestedPage).c_str();
  if (!fetch(manifestUrl, manifest)) {
    if (updateError == "HTTP 503") {
      LOG_INFO("Page unavailable (503); advancing pagination");
      savePages(Gerty::pageAfterUnavailable(requestedPage, savedPageCount), savedPageCount);
    }
    // The page list may have shrunk since the previous wake.
    if (updateError == "HTTP 404" && requestedPage != 0) savePages(0, 0);
    updateError = "JSON: " + updateError;
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, manifest.data, manifest.used)) return fail("Invalid JSON");
  if (!doc["schema_version"].is<int>() || doc["schema_version"].as<int>() != 1 ||
      !doc["refresh_seconds"].is<uint32_t>() ||
      doc["refresh_seconds"].as<uint32_t>() == 0 ||
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
  refreshSeconds = doc["refresh_seconds"].as<uint32_t>();
  String identity = url + "\n" + revision;
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(identity.c_str()),
                     identity.length(), digest, 0);
  if (hasImage && memcmp(digest, lastIdentity, sizeof(digest)) == 0) {
    LOG_INFO("Image unchanged");
    savePages(nextPage, pageCount);
    return true;
  }
  BoundedBuffer image(Config::MAX_PNG_BYTES, true);
  if (!fetch(url, image)) {
    if (updateError == "HTTP 503") {
      LOG_INFO("Image unavailable (503); saving next_page=%u", nextPage);
      savePages(nextPage, pageCount);
    }
    updateError = "Image: " + updateError;
    return false;
  }
#ifdef GERTY_WAVESHARE_C6
  image.file.close();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF); // Release network memory before allocating the RGB frame.
  int openResult = png.open("/download.png", openPngFile, closePngFile,
                            readPngFile, seekPngFile, drawLine);
#else
  int openResult = png.openRAM(image.data, image.used, drawLine);
#endif
  if (openResult != PNG_SUCCESS) {
    LOG_ERROR("PNG open failed: code=%d", openResult);
    return fail("PNG open error " + String(openResult));
  }
  LOG_DEBUG("PNG: %dx%d depth=%d type=%d interlaced=%d",
                png.getWidth(), png.getHeight(), png.getBpp(),
                png.getPixelType(), png.isInterlaced());
  // Restrict 16-bit-per-channel files as well as interlaced files in this POC.
  if (png.getWidth() != Display::WIDTH || png.getHeight() != Display::HEIGHT ||
      png.isInterlaced() || png.getBpp() > 8) {
    LOG_ERROR("PNG format rejected: requires %dx%d, non-interlaced, <=8 bits/channel", Display::WIDTH, Display::HEIGHT);
    png.close();
    return fail("PNG format unsupported");
  }
  framebuffer = static_cast<uint8_t *>(imageAlloc(Display::BUFFER_BYTES));
  if (!framebuffer) {
    LOG_ERROR("PNG framebuffer allocation failed");
    png.close();
    return fail("Not enough image memory");
  }
  memset(framebuffer, 0xFF, Display::BUFFER_BYTES);
  decodedRows = 0;
  int result = png.decode(nullptr, PNG_CHECK_CRC);
  png.close();
  bool ok = result == PNG_SUCCESS && decodedRows == Display::HEIGHT;
  if (!ok) fail("PNG decode error " + String(result));
  LOG_DEBUG("PNG decode: code=%d rows=%d/%d", result, decodedRows, Display::HEIGHT);
  if (ok) {
    ok = Display::present(framebuffer);
    if (!ok) fail("Display refresh failed");
  }
  if (ok) {
    memcpy(lastIdentity, digest, sizeof(digest));
    hasImage = true;
    savePages(nextPage, pageCount);
    shownError[0] = '\0';
    LOG_INFO("Display refresh completed; page saved");
  }
  free(framebuffer);
  framebuffer = nullptr;
  return ok;
}

void updateCycle() {
  updateError = "";
  bool ok = false;
  if (!displayReady) fail("Display initialization failed");
  else if (
#ifdef GERTY_WAVESHARE_C6
      true
#else
      psramFound()
#endif
  ) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    LOG_INFO("Connecting to Wi-Fi...");
    WiFi.begin(Provisioning::ssid.c_str(), Provisioning::password.c_str());
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
  Display::idle();
  if (Display::SUPPORTS_DEEP_SLEEP && Config::DEEP_SLEEP_ENABLED) {
    LOG_INFO("Sleeping %u seconds", sleepSeconds);
    if (Config::LOG_LEVEL != Config::LogLevel::NONE) Serial.flush();
    esp_sleep_enable_timer_wakeup(uint64_t(sleepSeconds) * 1000000ULL);
    esp_deep_sleep_start();
  } else {
    LOG_INFO("Staying awake; next check in %u seconds", sleepSeconds);
    uint64_t remaining = uint64_t(sleepSeconds) * 1000ULL;
    while (remaining > 0) {
      const uint32_t started = millis();
      if (Display::nextPageTapped()) {
        LOG_INFO("Screen tapped; requesting next page=%u", requestedPage);
        break;
      }
      Provisioning::poll();
      delay(20);
      uint32_t elapsed = millis() - started;
      remaining = elapsed >= remaining ? 0 : remaining - elapsed;
    }
  }
}

void showSetupInstructions() {
  displayReady = Display::begin();
  if (!displayReady || !Display::showSetup()) {
    LOG_ERROR("Cannot show setup instructions; USB configuration is still available");
  }
}

void setup() {
  Serial.begin(115200);
  if (Config::LOG_LEVEL != Config::LogLevel::NONE) {
    // Allow USB to attach on reset, without delaying normal timer wakes.
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
      uint32_t started = millis();
      while (!Serial && millis() - started < 1500) delay(50);
    }
  }
#ifdef GERTY_RELEASE
  Provisioning::begin("", "", "", showSetupInstructions);
#else
  Provisioning::begin(WIFI_SSID, WIFI_PASSWORD, Config::MANIFEST_URL, showSetupInstructions);
#endif
  LOG_INFO("Gerty boot; reset=%d; PSRAM=%u bytes", esp_reset_reason(), ESP.getPsramSize());
  LOG_DEBUG("Initializing display driver...");
  if (!displayReady) displayReady = Display::begin();
  if (!displayReady) LOG_ERROR("Display initialization failed");
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
