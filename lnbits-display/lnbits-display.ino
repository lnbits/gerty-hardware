#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "epd_driver.h"
#include "firasans.h"
#include "Digii40.h"
#include "Digii20.h"
#include "esp_adc_cal.h"
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>

#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM !!!"
#endif

using namespace std;

#define BATT_PIN            36
#define SD_MISO             12
#define SD_MOSI             13
#define SD_SCLK             14
#define SD_CS               15

uint8_t *framebuffer;
int vref = 1100;

void setup()
{
    char buf[128];
    Serial.begin(115200);
   
//  // Set WiFi to station mode and disconnect from an AP if it was previously connected
//  WiFi.mode(WIFI_STA);
//  WiFi.disconnect();
//  delay(100);

    /*
    * SD Card test
    * Only as a test SdCard hardware, use example reference
    * https://github.com/espressif/arduino-esp32/tree/master/libraries/SD/examples
    * * */
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
    bool rlst = SD.begin(SD_CS);
    if (!rlst) {
        Serial.println("SD init failed");
        snprintf(buf, 128, "➸ No detected SdCard");
    } else {
        snprintf(buf, 128, "➸ Detected SdCard insert:%.2f GB", SD.cardSize() / 1024.0 / 1024.0 / 1024.0);
    }

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("alloc memory failed !!!");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    epd_poweron();
    epd_clear();
    epd_poweroff();
}

void loop()
{
  initWiFi();
  mempoolSpace();
  Serial.println("Going to sleep");
  esp_sleep_enable_timer_wakeup(60 * 1000 * 1000);
  esp_deep_sleep_start();
  Serial.println("Waking up");
  sleep(60000);
}

// TODO: Fix this crappy code
void initWiFi() {
  WiFi.mode(WIFI_STA);
WiFi.begin("Maddox-Guest", "MadGuest1");
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

WiFiClientSecure client;

void mempoolSpace() {
  HTTPClient http;
  client.setInsecure();

  const char * headerKeys[] = {"date"} ;
  const size_t numberOfHeaders = 1;

  Serial.println("Getting data");
  // Send request
  //http.begin(client, "http://arduinojson.org/example.json");
  http.begin(client, "https://mempool.space/api/v1/fees/recommended");
  http.collectHeaders(headerKeys, numberOfHeaders);
  http.GET();
  
  // Print the response
  Serial.println("Got data");

  String responseDate = http.header("date");
  
  // Print the response
  String data = http.getString();
  Serial.print(data);

  Serial.print("Getting JSON");
  StaticJsonDocument<200> doc;
  Serial.println("Declared doc");
  DeserializationError error = deserializeJson(doc, data);
  Serial.println("deserialised");
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    Serial.println("Error deserializing");
    return;
  }

  String fastestFee = String(doc["fastestFee"].as<long>());
  String halfHourFee = String(doc["halfHourFee"].as<long>());
  String hourFee = String(doc["hourFee"].as<long>());
  String minimumFee = String(doc["minimumFee"].as<long>());
  
  Serial.println("Fastest fee " + fastestFee + " sat/vB");
  // Disconnect
  http.end();

  displayData(fastestFee, halfHourFee, hourFee, minimumFee);
  displayLastUpdateTime(responseDate);
}

void displayData(String fastestFee,String  halfHourFee, String hourFee,String  minimumFee) {
    int cursor_x = 200;
    int cursor_x_fees = 400;
    int cursor_y = 120;

    String feeEnd = " sats/vbyte\n";
    
    String fastestTitle = "Fastest";
    String fastestFees = fastestFee + feeEnd;
    
    String halfTitle = "Half hour";
    String halfFees = halfHourFee + feeEnd;

    String hourTitle = "Hour";
    String hourFees = hourFee + feeEnd;

    String minTitle = "Minimum";
    String minFees = minimumFee + feeEnd;
    
    epd_poweron();
//    epd_clear();
//    clearLine(cursor_x, cursor_y);

    Rect_t area = {
        .x = 182,
        .y = 40,
        .width = 576,
        .height = 305,
    };
    epd_clear_area(area);
    
    writeln((GFXfont *)&Digii40, "Mempool stats", &cursor_x, &cursor_y, NULL);
    
    cursor_x = 200;
    cursor_x_fees = 400;
    cursor_y += 75;
//    clearLine(cursor_x, cursor_y);
    writeln((GFXfont *)&Digii, fastestTitle.c_str(), &cursor_x, &cursor_y, NULL);
    writeln((GFXfont *)&Digii, fastestFees.c_str(), &cursor_x_fees, &cursor_y, NULL);

    cursor_x = 200;
    cursor_x_fees = 400;
    cursor_y += 50;
//    clearLine(cursor_x, cursor_y);
    writeln((GFXfont *)&Digii, halfTitle.c_str(), &cursor_x, &cursor_y, NULL);
    writeln((GFXfont *)&Digii, halfFees.c_str(), &cursor_x_fees, &cursor_y, NULL);

    cursor_x = 200;
    cursor_x_fees = 400;
    cursor_y += 50;
//    clearLine(cursor_x, cursor_y);
    writeln((GFXfont *)&Digii, hourTitle.c_str(), &cursor_x, &cursor_y, NULL);
    writeln((GFXfont *)&Digii, hourFees.c_str(), &cursor_x_fees, &cursor_y, NULL);

    cursor_x = 200;
    cursor_x_fees = 400;
    cursor_y += 50;
//        clearLine(cursor_x, cursor_y);
//    writeln((GFXfont *)&Digii, minTitle.c_str(), &cursor_x, &cursor_y, NULL);
//    writeln((GFXfont *)&Digii, minFees.c_str(), &cursor_x_fees, &cursor_y, NULL);

    displayVoltage();
//    displayLastUpdateTime();

    epd_poweroff();
}

void displayVoltage() {
    // When reading the battery voltage, POWER_EN must be turned on
    epd_poweron();
    delay(10); // Make adc measurement more accurate
    uint16_t v = analogRead(BATT_PIN);
    float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    String voltage = String(battery_voltage) + "V";
    Serial.println(voltage);

    int cursor_x = 20;
    int cursor_y = 530;
    clearLine(cursor_x, cursor_y);
    writeln((GFXfont *)&Digii, (char *)voltage.c_str(), &cursor_x, &cursor_y, NULL);
}

void clearLine(int xPos, int yPos) {
      Rect_t area = {
        .x = xPos - 70,
        .y = yPos,
        .width = 500,
        .height = 70,
    };
    epd_clear_area(area);
}

void displayLastUpdateTime(String lastUpdateTime) {
  int cursor_x = 480;
  int cursor_y = 530;
  clearLine(cursor_x, cursor_y);
  writeln((GFXfont *)&Digii, (char *)lastUpdateTime.c_str(), &cursor_x, &cursor_y, NULL);
}
