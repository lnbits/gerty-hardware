#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "epd_driver.h"
#include "esp_adc_cal.h"
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>

#include "logo.h"
#include "poppins20.h"
#include "poppins40.h"
#include "access_point.h"

using WebServerClass = WebServer;
WebServerClass server;
AutoConnect portal(server);
AutoConnectConfig config;
AutoConnectAux elementsAux;
AutoConnectAux saveAux;

fs::SPIFFSFS &FlashFS = SPIFFS;
#define FORMAT_ON_FAIL true

#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM !!!"
#endif

using namespace std;

#define PARAM_FILE "/elements.json"
#define BATT_PIN            36
#define SD_MISO             12
#define SD_MOSI             13
#define SD_SCLK             14
#define SD_CS               15

String apPassword = "ToTheMoon1"; //default WiFi AP password
String gertyEndpoint = "https://raw.githubusercontent.com/blackcoffeexbt/lnbits-display-mock-api/master/api.json";

uint8_t *framebuffer;
int vref = 1100;

int displayWidth = 960;
int displayHeight = 540;

int sleepTime = 60000; // The time to sleep in milliseconds
int lastScreenDisplayed = 0;
StaticJsonDocument<2000> apiDataDoc;
int menuItemCheck[4] = {0, 0};
String selection;

int fontXOffsetSize20 = 150;
int fontYOffsetSize20 = 150;

void setup()
{
    char buf[128];
    Serial.begin(115200);

    FlashFS.begin(FORMAT_ON_FAIL);
  SPIFFS.begin(true);
   
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
  loadSettings();
  initWiFi();
  getData();
  updateSettings();
  displayData(0);
  delay(30000);
  displayData(1);
  delay(30000);
  displayData(2);
    delay(30000);
  displayData(3);
  delay(2000);
  Serial.println("Going to sleep");
  esp_sleep_enable_timer_wakeup(sleepTime * 1000);
  esp_deep_sleep_start();
  Serial.println("Waking up");
  sleep(sleepTime);
}

// TODO: Fix this  code
void initWiFi() {

    // check wifi status
  if (menuItemCheck[0] == 1 && WiFi.status() != WL_CONNECTED)
  {
    menuItemCheck[0] = -1;
  }
  else if (menuItemCheck[0] == -1 && WiFi.status() == WL_CONNECTED)
  {
    menuItemCheck[0] = 1;
  }

  // count menu items
  int menuItemsAmount = 0;

//   for (int i = 0; i < sizeof(menuItems) / sizeof(menuItems[0]); i++)
//   {
//     if (menuItemCheck[i] == 1)
//     {
//       menuItemsAmount++;
//       selection = menuItems[i];
//     }
//   }

    // no methods available
    if (menuItemsAmount < 1)
    {
        Serial.println("Please configure device");
        //delay(10000000);
    }

    // general WiFi setting
    config.autoReset = false;
    config.autoReconnect = true;
    config.reconnectInterval = 1; // 30s
    config.beginTimeout = 10000UL;


      // connect to configured WiFi
    config.autoRise = false;

    portal.join({elementsAux, saveAux});
    portal.config(config);
    portal.begin();

//   WiFi.mode(WIFI_STA);
  Serial.print(F("Connecting to WiFi .."));
  int connectionAttemptCount = 0;
  int maxWifiConnectAttempts = 3;
  while (WiFi.status() != WL_CONNECTED && (connectionAttemptCount < maxWifiConnectAttempts)) {
    // Serial.println(F("Wifi connection attempt number "));
    Serial.println(connectionAttemptCount);
    delay(3000);
    connectionAttemptCount++;
  }

// launch AP portal
  if(connectionAttemptCount == maxWifiConnectAttempts) {
    launchAccessPoint();
  } else {
    Serial.println(WiFi.localIP());
  }
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

    Serial.println(F("Getting data"));
    // Send request
    http.begin(client, gertyEndpoint);
    http.collectHeaders(headerKeys, numberOfHeaders);
    http.GET();

    // Print the response
    Serial.println(F("Got data"));

    String responseDate = http.header("date");

    // Print the response
    String data = http.getString();
    Serial.print(data);

    Serial.print("Getting JSON");
    Serial.println(F("Declared doc"));
    DeserializationError error = deserializeJson(apiDataDoc, data);
    Serial.println("deserialised");
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        Serial.println(F("Error deserializing"));
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
    Serial.println(F("number of screens"));
    Serial.println(numberOfScreens);

    if(screenNumber > (numberOfScreens - 1)) {
        screenNumber = 0;
    }
    Serial.println(F("Getting screen number"));
    Serial.println(screenNumber);

    int i = 0;
    for (JsonObject elem : apiDataDoc["displayScreens"].as<JsonArray>()) {
        if(i == screenNumber) {
            Serial.println(F("Displaying screen"));
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
        writeln((GFXfont *)&poppins40, value, &posX, &posY, NULL);
    }
    else {
        writeln((GFXfont *)&poppins20, value, &posX, &posY, NULL);
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
    writeln((GFXfont *)&poppins20, (char *)voltage.c_str(), &cursor_x, &cursor_y, NULL);
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

void loadSettings() {
    // get the saved details and store in global variables
  File paramFile = FlashFS.open(PARAM_FILE, "r");
  if (paramFile)
  {
    StaticJsonDocument<2500> doc;
    DeserializationError error = deserializeJson(doc, paramFile.readString());

    const JsonObject passRoot = doc[0];
    const char *apPasswordChar = passRoot["value"];
    const char *apNameChar = passRoot["name"];
    Serial.println("AP password set to");
    Serial.println(apPasswordChar);
    if (String(apPasswordChar) != "" && String(apNameChar) == "ap_password")
    {
      apPassword = apPasswordChar;
    }
    Serial.println(apPassword);

    const JsonObject gertyEndpointRoot = doc[1];
    const char *gertyEndpointChar = gertyEndpointRoot["value"];
    gertyEndpoint = gertyEndpointChar;
  }

  paramFile.close();
}

/**
 * Display some nice stuff on the display
 */
void showPortalLaunch()
{
    Serial.println("Show portal launch information here");
    epd_poweron();
    epd_clear();
    int posX = 40;
    int posY = 100;
    writeln((GFXfont *)&poppins20, "Connect to access point to configure WiFi and Gerty's settings", &posX, &posY, NULL);
    epd_poweroff();
}

void launchAccessPoint() {
    
    Serial.println("Launching AP");

      // handle access point traffic
    server.on("/", []() {
      String content = "<h1>Gerty</br>Your Bitcoin Assistant</h1>";
      content += AUTOCONNECT_LINK(COG_24);
      server.send(200, "text/html", content);
    });

    elementsAux.load(FPSTR(PAGE_ELEMENTS));

    saveAux.load(FPSTR(PAGE_SAVE));
    saveAux.on([](AutoConnectAux &aux, PageArgument &arg) {
    aux["caption"].value = PARAM_FILE;
    File param = FlashFS.open(PARAM_FILE, "w");

    if (param)
    {
        // save as a loadable set for parameters.
        elementsAux.saveElement(param, {"ap_password", "gerty_endpoint"});
        param.close();

        // read the saved elements again to display.
        param = FlashFS.open(PARAM_FILE, "r");
        aux["echo"].value = param.readString();
        param.close();
    }
    else
    {
        aux["echo"].value = "Filesystem failed to open.";
    }

    return String();
    });

    elementsAux.on([](AutoConnectAux &aux, PageArgument &arg) {
      File param = FlashFS.open(PARAM_FILE, "r");
      if (param)
      {
        aux.loadElement(param, {"ap_password", "gerty_endpoint"});
        param.close();
      }

      if (portal.where() == "/gerty")
      {
        File param = FlashFS.open(PARAM_FILE, "r");
        if (param)
        {
          aux.loadElement(param, {"ap_password", "gerty_endpoint"});
          param.close();
        }
      }
      return String();
    });

    Serial.println("Launching AP2");
    config.immediateStart = true;
    config.ticker = true;
    config.apid = "Gerty-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    config.psk = apPassword;
    config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
    config.title = "Gerty";

    showPortalLaunch();

    portal.join({elementsAux, saveAux});
    portal.config(config);
    portal.begin();
    while (true)
    {
      portal.handleClient();
    }
}