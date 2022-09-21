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
// #include <SD.h>
#include <esp_sleep.h>
#include "qrcoded.h"

#include "smile.h"
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

String spiffing;
String apPassword = "ToTheMoon1"; //default WiFi AP password
String gertyEndpoint = "https://gerty.yourtemp.net/api/screen/0";
String qrData;

uint8_t *framebuffer;
int vref = 1100;

int sleepTime = 300; // The time to sleep in seconds
int lastScreenDisplayed = 0;
StaticJsonDocument<3000> apiDataDoc;
String selection;

int fontXOffsetSize20 = 150;
int fontYOffsetSize20 = 150;

int portalPin = 13;
int triggerAp = false;

void setup()
{
    char buf[128];
    Serial.begin(115200);

    FlashFS.begin(FORMAT_ON_FAIL);
    SPIFFS.begin(true);
   
 // Set WiFi to station mode and disconnect from an AP if it was previously connected
//  WiFi.mode(WIFI_STA);
//  WiFi.disconnect();
//  delay(100);

    // Serial.println(F("touch pin value is"));
    // Serial.println(touchRead(portalPin));
    // if(touchRead(portalPin) < 60){
    //     // Serial.println("Launch portal");
    //     triggerAp = true;
    // }

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        // Serial.println("alloc memory failed !!!");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

void showSplash() {
    epd_poweron();

    epd_clear();
    Rect_t area = {
        .x = 260,
        .y = 300,
        .width = smile_width,
        .height = smile_height,
    };
    epd_copy_to_framebuffer(area, (uint8_t *)smile_data, framebuffer);
    epd_fill_circle(356, 171, 45, 0, framebuffer);
    epd_fill_circle(600, 171, 45, 0, framebuffer);
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);

    delay(1000);
    epd_poweroff();
}

void loop()
{
  int screenToDisplay = 0;
  screenToDisplay = loadScreenToDisplay();
  screenToDisplay++;
  
  initWiFi();
  loadSettings();
  getData();
  updateSettings();
  showSplash();
  displayData(screenToDisplay);
  displayVoltage();
  delay(500);

  // Serial.println("Going to sleep for " + String(sleepTime / 1000000) + " seconds");
  esp_sleep_enable_timer_wakeup(sleepTime * 1000 * 1000);
  esp_deep_sleep_start();
  // Serial.println("This should never be hit");
  sleep(sleepTime);
}

int loadScreenToDisplay() {
  File file = SPIFFS.open("/config.txt");
   spiffing = file.readStringUntil('\n');
  String tempScreenToDisplay = spiffing.c_str();
  int tempScreenToDisplayInt = tempScreenToDisplay.toInt();
  // Serial.println("spiffcontent " + String(tempScreenToDisplayInt));
  file.close();
  // Serial.println("screenToDisplay from config " + String(tempScreenToDisplayInt));
  return tempScreenToDisplayInt;
}

void setNextScreenToDisplay(int screenToDisplay) {
  File configFile = SPIFFS.open("/config.txt", "w");
  configFile.print(String(screenToDisplay));
  configFile.close();
}

// TODO: Fix this  code
void initWiFi() {
    // general WiFi setting
    configureAccessPoint();
    // portal.whileCaptivePortal(whileCP);
    portal.begin();

  WiFi.mode(WIFI_STA);
  // Serial.println("Connecting to WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(3000);
  }
  // Serial.println("Connected to WiFi");

  // Serial.println(WiFi.localIP());
}

bool whileCP(void) {
  bool  rc;
  showAPLaunchScreen();
  // Here, something to process while the captive portal is open.
  // To escape from the captive portal loop, this exit function returns false.
  // rc = true;, or rc = false;
  // rc = true;
  // while(true) {}
  return rc;
}

void configureAccessPoint() {

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

    config.autoReset = false;
    config.autoReconnect = true;
    config.reconnectInterval = 1; // 30s
    config.beginTimeout = 10000UL;

    // Enable AP on wifi connection failure
    config.autoRise = true;
    config.immediateStart = triggerAp;
    config.apid = "Gerty-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    config.psk = apPassword;
    config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
    config.title = "Gerty";

    portal.join({elementsAux, saveAux});
    portal.config(config);
    
    // showPortalLaunch();
    // while (true)
    // {
    // //   portal.handleClient();
    // }
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

    // Serial.println("Getting data from " + gertyEndpoint);
    // Send request
    http.begin(client, gertyEndpoint);
    http.collectHeaders(headerKeys, numberOfHeaders);
    http.GET();

    // Print the response
    // Serial.println("Got data");

    String responseDate = http.header("date");

    // Print the response
    String data = http.getString();
    Serial.print(data);

    Serial.print("Getting JSON");
    // Serial.println("Declared doc");
    DeserializationError error = deserializeJson(apiDataDoc, data);
    // Serial.println("deserialised");
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        // Serial.println(error.f_str());
        // Serial.println("Error deserializing");
        return;
    }
    // Disconnect
    http.end();
}

/**
 * Update device settings using data from the API
 */
void updateSettings() {
    // sleepTime = 30000;
}

/**
 * Display the data for the specified screen 
 */
void displayData(int screenNumber) {
    epd_poweron();
    epd_clear();
    //get settings
     int nextScreen = apiDataDoc["settings"]["nextScreenNumber"];
     sleepTime = apiDataDoc["settings"]["refreshTime"];
     Serial.println(F("Next screen is"));
     Serial.println(nextScreen);
    
    setNextScreenToDisplay(screenNumber);

    const char* slug = apiDataDoc["screen"]["slug"]; 
    Serial.println("Slug");
    Serial.println(slug);

    const char* group = apiDataDoc["screen"]["group"]; 
    Serial.println("Group");
    Serial.println(group);

    for (JsonObject textElem : apiDataDoc["screen"]["text"].as<JsonArray>()) {
        Serial.println("text");
        const char* value = textElem["value"]; 
        Serial.println(value);
        // renderText(textElem);
    }
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff();
}

int posX;
int posY;
int fontSize;

void renderText(JsonObject textElem) {
    const char* value = textElem["value"]; 
    // Serial.println(value);

    fontSize = textElem["size"]; 

    posX = textElem["x"];
    posX = posX; 

    posY = textElem["y"]; 
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
    // Serial.println(voltage);

    int cursor_x = 20;
    int cursor_y = 500;
    clearLine(cursor_x, cursor_y);
    writeln((GFXfont *)&poppins20, (char *)voltage.c_str(), &cursor_x, &cursor_y, NULL);
    epd_poweroff();
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
    StaticJsonDocument<3000> doc;
    DeserializationError error = deserializeJson(doc, paramFile.readString());

    const JsonObject passRoot = doc[0];
    const char *apPasswordChar = passRoot["value"];
    const char *apNameChar = passRoot["name"];
    // Serial.println("AP password set to");
    // Serial.println(apPasswordChar);
    if (String(apPasswordChar) != "" && String(apNameChar) == "ap_password")
    {
      apPassword = apPasswordChar;
    }
    // Serial.println(apPassword);

    const JsonObject gertyEndpointRoot = doc[1];
    const char *gertyEndpointChar = gertyEndpointRoot["value"];
    if (String(gertyEndpointChar) != "") {
        gertyEndpoint = gertyEndpointChar;
    }
  }

  paramFile.close();
}

/**
 * Show the Access Point configuration prompt screen
 */
void showAPLaunchScreen()
{
  // Serial.println("Show portal launch information here");
  qrData = "WIFI:S:" + config.apid + ";T:WPA;P:" + apPassword;
  const char *qrDataChar = qrData.c_str();
  QRCode qrcoded;
  
  int qrVersion = getQrCodeVersion();
  int pixSize = getQrCodePixelSize(qrVersion);
  uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];

  qrcode_initText(&qrcoded, qrcodeData, qrVersion, 0, qrDataChar);

  epd_poweron();
  epd_clear();

  int qrWidth = pixSize * qrcoded.size;
  Serial.println(F("qrWidth"));
  Serial.println(qrWidth);
  int qrPosX = ((EPD_WIDTH - qrWidth) / 2);
  int qrPosY = ((EPD_HEIGHT - qrWidth) / 2);
  Serial.println("EPD_WIDTH - qrWidth");
  // calculate the center of the screen
    // Serial.println(qrPosX);
    Serial.println(EPD_WIDTH);
    Serial.println(qrPosX);

  for (uint8_t y = 0; y < qrcoded.size; y++)
  {
    for (uint8_t x = 0; x < qrcoded.size; x++)
    {
      if (qrcode_getModule(&qrcoded, x, y))
      {
        epd_fill_rect(qrPosX + pixSize * x, qrPosY + pixSize * y, pixSize, pixSize, 0, framebuffer);
      }
    }
  }

  epd_draw_grayscale_image(epd_full_screen(), framebuffer);
  posX = 120;
  posY = 50;
  writeln((GFXfont *)&poppins20, "No Internet connection available", &posX, &posY, NULL);
  posX = 120;
  posY = 495;
  writeln((GFXfont *)&poppins20, String("Connect to AP " + config.apid).c_str(), &posX, &posY, NULL);
  posX = 120;
  posY = 535;
  writeln((GFXfont *)&poppins20, String("With password \"" + apPassword + "\"").c_str(), &posX, &posY, NULL);
  epd_poweroff();
}

/**
 * @brief Get the size of the qr code to produce
 * 
 * @param qrData 
 * @return int 
 */
int getQrCodeVersion() {
  int qrVersion = 0;
  int stringLength = qrData.length();

  // Using this chart with ECC_LOW https://github.com/ricmoo/QRCode#data-capacities
  if(stringLength <= 17) {
    qrVersion = 1;
  }  
  else if(stringLength <= 32) {
    qrVersion = 2;
  }
  else if(stringLength <= 53) {
    qrVersion = 3;
  }
  else if(stringLength <= 134) {
    qrVersion = 6;
  }
  else if(stringLength <= 367) {
    qrVersion = 11;
  }
  else {
    qrVersion = 28;
  }

  // Serial.println(F("QR version to use"));
  // Serial.println(qrVersion);
  return qrVersion;
}


/**
 * @brief Get the Qr Code Pixel Size object
 * 
 * @param qrCodeVersion The QR code version that is being used
 * @return int The size of the QR code pixels
 */
int getQrCodePixelSize(int qrCodeVersion) {
  int qrDisplayHeight = 300; // qr code height in pixels
  // Using https://github.com/ricmoo/QRCode#data-capacities

  // Get the QR code size (blocks not pixels)
  int qrCodeHeight = 0;
  switch(qrCodeVersion) {
    case 1:
      qrCodeHeight = 21;
      break;
    case 2:
      qrCodeHeight = 25;
      break;
    case 3:
      qrCodeHeight = 29;
      break;
    case 4:
      qrCodeHeight = 33;
      break;
    case 5:
      qrCodeHeight = 37;
      break;
    case 6:
      qrCodeHeight = 41;
      break;
    case 7:
      qrCodeHeight = 45;
      break;
    case 8:
      qrCodeHeight = 49;
      break;
    case 9:
      qrCodeHeight = 53;
      break;
    case 10:
      qrCodeHeight = 57;
      break;
    case 11:
      qrCodeHeight = 61;
      break;
    default:
      qrCodeHeight = 129;
      break;
  }
  int pixelHeight = floor(qrDisplayHeight / qrCodeHeight);
  Serial.println(F('qrCodeHeight pixel height is'));
  Serial.println(qrCodeHeight);

  Serial.println(F('Calced pixel height is'));
  Serial.println(pixelHeight);
  return pixelHeight;
}