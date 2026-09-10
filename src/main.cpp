#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "epd_driver.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

RTC_DATA_ATTR uint8_t lastIdentity[32] = {};
RTC_DATA_ATTR bool hasImage = false;
RTC_DATA_ATTR uint32_t refreshSeconds = Config::DEFAULT_REFRESH_SECONDS;
RTC_DATA_ATTR uint32_t failures = 0;
PNG png;
uint8_t *framebuffer = nullptr;
int decodedRows = 0;

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
  Serial.print("HTTPS GET: ");
  Serial.println(url);
  if (!url.startsWith("https://") || !body.data) return false;
  WiFiClientSecure client;
  client.setInsecure(); // HTTPS encryption without certificate verification.
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setConnectTimeout(Config::HTTP_TIMEOUT_MS);
  http.setTimeout(Config::HTTP_TIMEOUT_MS);
  // Require direct HTTPS URLs, avoiding redirects to untrusted transports.
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == HTTP_CODE_OK && http.getSize() <= static_cast<int>(body.capacity)) {
    int received = http.writeToStream(&body);
    ok = received > 0 && static_cast<size_t>(received) == body.used;
  }
  Serial.printf("GET status=%d bytes=%u success=%d\n", code, body.used, ok);
  http.end();
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
  if (!fetch(Config::MANIFEST_URL, manifest)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, manifest.data, manifest.used)) return false;
  if (!doc["schema_version"].is<int>() || doc["schema_version"].as<int>() != 1 ||
      !doc["refresh_seconds"].is<uint32_t>() ||
      !doc["image_url"].is<const char *>() ||
      !doc["image_revision"].is<const char *>()) return false;
  String url = doc["image_url"].as<String>();
  String revision = doc["image_revision"].as<String>();
  if (!url.startsWith("https://") || url.length() > 2048 ||
      revision.isEmpty() || revision.length() > 256) return false;
  refreshSeconds = constrain(doc["refresh_seconds"].as<uint32_t>(),
                             Config::MIN_REFRESH_SECONDS, Config::MAX_REFRESH_SECONDS);
  String identity = url + "\n" + revision;
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(identity.c_str()),
                     identity.length(), digest, 0);
  if (hasImage && memcmp(digest, lastIdentity, sizeof(digest)) == 0) {
    Serial.println("Image unchanged");
    return true;
  }
  BoundedBuffer image(Config::MAX_PNG_BYTES);
  if (!fetch(url, image)) return false;
  int openResult = png.openRAM(image.data, image.used, drawLine);
  if (openResult != PNG_SUCCESS) {
    Serial.printf("PNG open failed: code=%d\n", openResult);
    return false;
  }
  Serial.printf("PNG: %dx%d depth=%d type=%d interlaced=%d\n",
                png.getWidth(), png.getHeight(), png.getBpp(),
                png.getPixelType(), png.isInterlaced());
  // Restrict 16-bit-per-channel files as well as interlaced files in this POC.
  if (png.getWidth() != EPD_WIDTH || png.getHeight() != EPD_HEIGHT ||
      png.isInterlaced() || png.getBpp() > 8) {
    Serial.println("PNG format rejected: requires 960x540, non-interlaced, <=8 bits/channel");
    png.close();
    return false;
  }
  framebuffer = static_cast<uint8_t *>(ps_malloc(EPD_WIDTH * EPD_HEIGHT / 2));
  if (!framebuffer) {
    Serial.println("PNG framebuffer allocation failed");
    png.close();
    return false;
  }
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
  decodedRows = 0;
  int result = png.decode(nullptr, PNG_CHECK_CRC);
  png.close();
  bool ok = result == PNG_SUCCESS && decodedRows == EPD_HEIGHT;
  Serial.printf("PNG decode: code=%d rows=%d/%d\n", result, decodedRows, EPD_HEIGHT);
  if (ok) {
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff_all();
    memcpy(lastIdentity, digest, sizeof(digest));
    hasImage = true;
    Serial.println("Image displayed");
  }
  free(framebuffer);
  framebuffer = nullptr;
  return ok;
}

void updateCycle() {
  bool ok = false;
  if (psramFound()) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - started < Config::WIFI_TIMEOUT_MS) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Wi-Fi connected; IP: ");
      Serial.println(WiFi.localIP());
      ok = updateImage();
    } else Serial.printf("Wi-Fi connection failed; status=%d\n", WiFi.status());
  } else Serial.println("PSRAM unavailable");
  uint32_t sleepSeconds = refreshSeconds;
  if (ok) failures = 0;
  else {
    failures = min(failures + 1, uint32_t(5));
    sleepSeconds = min(uint32_t(30) << (failures - 1), uint32_t(300));
    Serial.println("Update failed; keeping previous image");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  epd_poweroff_all();
#ifdef GERTY_USB_DEBUG
  Serial.printf("USB debug: staying awake for %u seconds\n", sleepSeconds);
  for (uint32_t seconds = 0; seconds < sleepSeconds; ++seconds) {
    if (seconds % 5 == 0) Serial.printf("USB debug alive; next check in %u seconds\n", sleepSeconds - seconds);
    delay(1000);
  }
#else
  Serial.printf("Sleeping %u seconds\n", sleepSeconds);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(uint64_t(sleepSeconds) * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void setup() {
  Serial.begin(115200);
#ifdef GERTY_USB_DEBUG
  uint32_t started = millis();
  while (!Serial && millis() - started < 8000) delay(100);
  delay(500);
#endif
  Serial.printf("\nGerty boot; reset=%d; PSRAM=%u bytes\n", esp_reset_reason(), ESP.getPsramSize());
  Serial.println("Initializing display driver...");
  epd_init();
  epd_poweroff_all();
  Serial.println("Display driver initialized");
  // A reset/upload forces a redraw, while timer wakes retain the revision.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    hasImage = false;
    refreshSeconds = Config::DEFAULT_REFRESH_SECONDS;
    failures = 0;
  }
}

void loop() { updateCycle(); }
