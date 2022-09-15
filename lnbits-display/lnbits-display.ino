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
#include "logo.h"

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

int displayWidth = 960;
int displayHeight = 540;

int sleepTime = 60000; // The time to sleep in milliseconds
int lastScreenDisplayed = 0;
StaticJsonDocument<2000> apiDataDoc;

int fontXOffsetSize20 = 150;
int fontYOffsetSize20 = 150;

void setup()
{
    char buf[128];
    Serial.begin(115200);
   
//  // Set WiFi to station mode and disconnect from an AP if it was previously connected
//  WiFi.mode(WIFI_STA);
//  WiFi.disconnect();
//  delay(100);

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("alloc memory failed !!!");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    epd_poweron();
    showSplash();
    epd_poweroff();
}

void showSplash() {
    epd_clear();
    Rect_t area = {
        .x = (displayWidth - logo_width) / 2,
        .y = (displayHeight - logo_height) / 2,
        .width = logo_width,
        .height = logo_height,
    };
    epd_draw_image(area, (uint8_t *)logo_data, BLACK_ON_WHITE);
    delay(1000);
}

void loop()
{
  delay(5000);
  initWiFi();
  getData();
  updateSettings();
  displayData(0);
  delay(5000);
  displayData(1);
  delay(5000);
  displayData(2);
    delay(5000);
  displayData(3);
  delay(2000);
  Serial.println("Going to sleep");
  esp_sleep_enable_timer_wakeup(sleepTime * 1000);
  esp_deep_sleep_start();
  Serial.println("Waking up");
  sleep(sleepTime);
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

/**
 * This gets the JSON data from the LNbits endpoint and serialises it into
 * memory to be used by the microcontroller
 */
void getData() {
    HTTPClient http;
    client.setInsecure();

    const char * headerKeys[] = {"date"} ;
    const size_t numberOfHeaders = 1;

    Serial.println("Getting data");
    // Send request
    http.begin(client, "https://raw.githubusercontent.com/blackcoffeexbt/lnbits-display-mock-api/master/api.json");
    http.collectHeaders(headerKeys, numberOfHeaders);
    http.GET();

    // Print the response
    Serial.println("Got data");

    String responseDate = http.header("date");

    // Print the response
    String data = http.getString();
    Serial.print(data);

    Serial.print("Getting JSON");
    Serial.println("Declared doc");
    DeserializationError error = deserializeJson(apiDataDoc, data);
    Serial.println("deserialised");
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        Serial.println("Error deserializing");
        return;
    }
    // Disconnect
    http.end();
}

/**
 * Update device settings using data from the API
 */
void updateSettings() {
    sleepTime = 30000;
}

/**
 * Display the data for the specified screen 
 */
void displayData(int screenNumber) {
    epd_poweron();
    epd_clear();
    
    int numberOfScreens = 0;

    // count the number of screens
    for (JsonObject elem : apiDataDoc["displayScreens"].as<JsonArray>()) {
        numberOfScreens++;
    }
    Serial.println("number of screens");
    Serial.println(numberOfScreens);

    if(screenNumber > (numberOfScreens - 1)) {
        screenNumber = 0;
    }
    Serial.println("Getting screen number");
    Serial.println(screenNumber);

    int i = 0;
    for (JsonObject elem : apiDataDoc["displayScreens"].as<JsonArray>()) {
        if(i == screenNumber) {
            Serial.println("Displaying screen");
            Serial.println(i);
            const char* slug = elem["slug"]; 
            Serial.println("Slug");
            Serial.println(slug);

            const char* group = elem["group"]; 
            Serial.println("group ");
            Serial.println(group);

            for (JsonObject textElem : elem["text"].as<JsonArray>()) {

                renderText(textElem);
            }
        }
        i++;
    }  
    epd_poweroff();
}

int posX;
int posY;
int fontSize;

void renderText(JsonObject textElem) {
    Serial.println("---------");
    const char* value = textElem["value"]; 
    Serial.println("value");
    Serial.println(value);

    fontSize = textElem["size"]; 
    // Serial.println("fontSize");
    Serial.println(fontSize);

    posX = textElem["x"];
    posX = posX; 
    // Serial.println("posX");
    Serial.println(posX);

    posY = textElem["y"]; 
    Serial.println("posY");
    // Serial.println(posY);
    posY = posY + fontYOffsetSize20;
            
    if(fontSize == 40) {
        writeln((GFXfont *)&Digii40, value, &posX, &posY, NULL);
    }
    else {
        writeln((GFXfont *)&Digii, value, &posX, &posY, NULL);
    }
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