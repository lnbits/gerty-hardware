#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#elif defined(ARDUINO_ARCH_ESP32)

#include <ArduinoJson.h>
#include <HTTPClient.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#endif
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
#include "poppins12.h"
#include "poppins15.h"
#include "poppins20.h"
#include "poppins40.h"
#include "poppins80.h"
#include "access_point.h"

// using WebServerClass = WebServer;
// WebServerClass server;

#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer server;
#elif defined(ARDUINO_ARCH_ESP32)
WebServer server;
#endif

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
String gertyEndpoint = "https://sats.pw/gerty/api/v1/gerty/cEMVzYuxCxYbBNu9ourou3";
String qrData;

uint8_t *framebuffer;
int vref = 1100;

int isFirstLine = true;
int sleepTime = 300; // The time to sleep in seconds
int showTextBoundRect = false;
StaticJsonDocument<1500> apiDataDoc;
String selection;

int textBoxStartX = 0;
int textBoxStartY = 0;
int lineSpacing = 100;
int firstLineOffset = 40;

int posX = 0;
int posY = 0;
int fontSize;

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
        Serial.println("alloc memory failed !!!");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    initWiFi();
}

void loop()
{
  isFirstLine = true;
  int screenToDisplay = 0;
  screenToDisplay = loadScreenNumberToDisplay();
  // if(screenToDisplay == 0) {
  //   refreshScreen();
  // }
  // Serial.println("Here");
  loadSettings();
  getData(screenToDisplay);
  // showSplash();
  displayData();
  // displayVoltage();
  // delay(500);
  
  displayLastUpdateTime();

   Serial.println("Going to sleep for " + String(sleepTime) + " seconds");
   esp_sleep_enable_timer_wakeup(sleepTime * 1000 * 1000);
   esp_deep_sleep_start();
   Serial.println("This should never be hit");
  delay(sleepTime * 1000);
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
    draw_framebuf(true);

    delay(1000);
    epd_poweroff();
}

int loadScreenNumberToDisplay() {
  File file = SPIFFS.open("/config.txt");
   spiffing = file.readStringUntil('\n');
  String tempScreenToDisplay = spiffing.c_str();
  int tempScreenToDisplayInt = tempScreenToDisplay.toInt();
  // Serial.println("spiffcontent " + String(tempScreenToDisplayInt));
  file.close();
  // Serial.println("screenToDisplay from config " + String(tempScreenToDisplayInt));
  return tempScreenToDisplayInt;
}

void saveNextScreenToDisplay(int screenToDisplay) {
  File configFile = SPIFFS.open("/config.txt", "w");
  configFile.print(String(screenToDisplay));
  configFile.close();
}

// TODO: Fix this  code
void initWiFi() {
    // general WiFi setting
    configureAccessPoint();
    portal.whileCaptivePortal(whileCP);
    portal.begin();

  WiFi.mode(WIFI_STA);
  // Serial.println("Connecting to WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    // Serial.print('.');
    delay(3000);
  }
  // Serial.println("Connected to WiFi");

  // Serial.println(WiFi.localIP());
}

bool whileCP(void) {
  bool rc;
  showAPLaunchScreen();
  rc = true;
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

//    config.autoReset = true;
//    config.autoReconnect = true;
//    config.reconnectInterval = 1; // 60s
//    config.beginTimeout = 10000UL;

//    // Enable AP on wifi connection failure
//    config.autoRise = true;
//    config.immediateStart = triggerAp;
//    config.apid = "Gerty-" + String((uint32_t)ESP.getEfuseMac(), HEX);
//    config.psk = apPassword;
//    config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
//    config.title = "Gerty";

// Enable saved past credential by autoReconnect option,
  // even once it is disconnected.
  config.autoReconnect = true;
  config.hostName = "esp32-01";

    portal.join({elementsAux, saveAux});
    portal.config(config);

    // Establish a connection with an autoReconnect option.
  if (portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
//    #if defined(ARDUINO_ARCH_ESP8266)
//    Serial.println(WiFi.hostname());
//    #elif defined(ARDUINO_ARCH_ESP32)
//    Serial.println(WiFi.getHostname());
//    #endif
  }
    
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
void getData(int screenToDisplay) {
    HTTPClient http;
    client.setInsecure();

    const char * headerKeys[] = {"date"} ;
    const size_t numberOfHeaders = 1;

    const String gertyEndpointWithScreenNumber = gertyEndpoint + "/" + screenToDisplay;
    // const String gertyEndpointWithScreenNumber = gertyEndpoint + "/0";
    // gertyEndpoint = gertyEndpoint;
    Serial.println("Getting data from " + gertyEndpointWithScreenNumber);
    // Send request
    http.begin(client, gertyEndpointWithScreenNumber);
    http.collectHeaders(headerKeys, numberOfHeaders);
    http.GET();

    String responseDate = http.header("date");

    // Print the response
    String data = http.getString();
    // Serial.println("JSON data");
    // Serial.println(data);

    DeserializationError error = deserializeJson(apiDataDoc, data);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    // Disconnect
    http.end();
}

/**
 * Display the data for the specified screen 
 */
void displayData() {
    epd_poweron();
    epd_clear();
    //get settings
     int nextScreen = apiDataDoc["settings"]["nextScreenNumber"];
     sleepTime = apiDataDoc["settings"]["refreshTime"];
    

     showTextBoundRect = apiDataDoc["settings"]["showTextBoundRect"];
      Serial.println(F("sleepTime is"));
      Serial.println(sleepTime);
    
    saveNextScreenToDisplay(nextScreen);

    const char* slug = apiDataDoc["screen"]["slug"]; 
    // Serial.println("Slug");
    // Serial.println(slug);

    const char* group = apiDataDoc["screen"]["group"]; 
    // Serial.println("Group");
    // Serial.println(group);


    setTextBoxCoordinates();

    posY = 0;
    isFirstLine = true;
    for (JsonObject textElem : apiDataDoc["screen"]["text"].as<JsonArray>()) {
        renderText(textElem);
        isFirstLine = false;
    }

    draw_framebuf(true);
    epd_poweroff();
}

void renderText(JsonObject textElem) {
  const char* value = textElem["value"]; 

  fontSize = textElem["size"]; 

  const String pos = textElem["position"];
//  Serial.print("Position");
//  Serial.print(pos);

  if(textElem["x"] != NULL && textElem["y"] != NULL) {
    posX = textElem["x"];
    posY = textElem["y"];
  } else {
    posX = textBoxStartX;
    // initialise the text box starting position if it hasnt been set
    if(posY == 0) {
      posY = textBoxStartY + firstLineOffset;
    }
    // add a line spacing if this isnt the first element
    if(!isFirstLine) {
      // Serial.println("Adding line spacing");
      posY += getLineSpacing(fontSize);
    }
  }

  // Serial.println(value);
  
  switch(fontSize) {
    case 12:
      write_string((GFXfont *)&poppins12, (char *)value, &posX, &posY, framebuffer);
      break;
    case 15:
      write_string((GFXfont *)&poppins15, (char *)value, &posX, &posY, framebuffer);
      break;
    case 20:
      write_string((GFXfont *)&poppins20, (char *)value, &posX, &posY, framebuffer);
      break;
    case 40:
      write_string((GFXfont *)&poppins40, (char *)value, &posX, &posY, framebuffer);
      break;
    case 80:
      write_string((GFXfont *)&poppins80, (char *)value, &posX, &posY, framebuffer);
      break;
    default:
      write_string((GFXfont *)&poppins20, (char *)value, &posX, &posY, framebuffer);
  }

  isFirstLine = false;
  
}

/**
 * Set the textBoxStartX and textBoxStartY coordinates to allow the content to be centred
 */
void setTextBoxCoordinates() {
//  uint freeRAM = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
//  ESP_LOGI(TAG, "free RAM is %d.", freeRAM);
  int totalTextHeight = 0;
  int totalTextWidth = 0;
  int posY = 0;
  int posX = 0;
  int endPosX = 0;
  int endPosY = 0;

  char* stringToSplit;
  char delimiter[] = "\n";
  char* ptr;

  isFirstLine = true;
  // for each text element in JSON array
    for (JsonObject textElem : apiDataDoc["screen"]["text"].as<JsonArray>()) {
      posX = 0;
      const char* value = textElem["value"]; 

      int textWidth = 0;
      int textHeight = 0;

      int endY = 0;
      int endX = 0;
      int textBoxWidth = 0;
      int textBoxHeight = 0;

      fontSize = textElem["size"];

      stringToSplit = copyString((char *)value);
      // initialize first part (string, delimiter)
      ptr = strtok((char *)stringToSplit, delimiter);

      int textBoundsEndX = 0;
      int textBoundsEndY = 0;
      int textBoundsWidth = 0;
      int textBoundsHeight = 0;

      // Serial.println("-----");
      while(ptr != NULL) {
          // Serial.println("found one part:");
          // Serial.println(ptr);

          switch(fontSize) {
            case 12:
              get_text_bounds((GFXfont *)&poppins12, (char *)stringToSplit, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
              break;
            case 15:
              get_text_bounds((GFXfont *)&poppins15, (char *)stringToSplit, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
              break;
            case 20:
              get_text_bounds((GFXfont *)&poppins20, (char *)stringToSplit, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
              break;
            case 40:
              get_text_bounds((GFXfont *)&poppins40, (char *)stringToSplit, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
              break;
            case 80:
              get_text_bounds((GFXfont *)&poppins80, (char *)stringToSplit, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
              break;
            default:
              get_text_bounds((GFXfont *)&poppins20, (char *)stringToSplit, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          }

            totalTextHeight += textBoundsHeight;
//            if(!isFirstLine) {
              // Serial.println("Adding line spacing");
              totalTextHeight += getLineSpacing(fontSize);
//            }
//            Serial.println("Text area height");
//            Serial.println(ptr);
//            Serial.println(totalTextHeight);

                  if(textBoundsWidth > totalTextWidth) {
                    totalTextWidth = textBoundsWidth;
                  }

          // Serial.println("Text width");
          // Serial.println(textBoundsWidth);
          // create next part
//          free(ptr);
          ptr = strtok(NULL, delimiter);
      }

//      Serial.println("Text area height");
//      Serial.println(totalTextHeight);

      switch(fontSize) {
        case 12:
          write_string((GFXfont *)&poppins12, (char *)value, &posX, &posY, framebuffer);
          break;
        case 15:
          write_string((GFXfont *)&poppins15, (char *)value, &posX, &posY, framebuffer);
          break;
        case 20:
          write_string((GFXfont *)&poppins20, (char *)value, &posX, &posY, framebuffer);
          break;
        case 40:
          write_string((GFXfont *)&poppins40, (char *)value, &posX, &posY, framebuffer);
          break;
        case 80:
          write_string((GFXfont *)&poppins80, (char *)value, &posX, &posY, framebuffer);
          break;
        default:
          write_string((GFXfont *)&poppins20, (char *)value, &posX, &posY, framebuffer);
      }
      
      // set starting X and Y coordinates for all text
      textBoxStartX = (EPD_WIDTH - totalTextWidth) / 2;
      if(textBoxStartX < 0) {
        textBoxStartX = 10;
      }
      
      textBoxStartY = (EPD_HEIGHT - totalTextHeight) / 2;
      if(textBoxStartY < 0) {
        textBoxStartY = 10;
      }
      isFirstLine = false;
  }

  clear_framebuf();
  if(showTextBoundRect) {
    epd_draw_rect(textBoxStartX, textBoxStartY, totalTextWidth, totalTextHeight, 0, framebuffer);
  }

}

void displayVoltage() {
    // When reading the battery voltage, POWER_EN must be turned on
    epd_poweron();
    delay(10); // Make adc measurement more accurate
    uint16_t v = analogRead(BATT_PIN);
    float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    String voltage = String(battery_voltage) + "V";

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
    StaticJsonDocument<1000> doc;
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

    const JsonObject gertyEndpointRoot = doc[1];
    const char *gertyEndpointChar = gertyEndpointRoot["value"];
    if (String(gertyEndpointChar) != "") {
        gertyEndpoint = gertyEndpointChar;
    }
  }

  paramFile.close();
}

bool isApScreenActive = false;
/**
 * Show the Access Point configuration prompt screen
 */
void showAPLaunchScreen()
{
  if(!isApScreenActive) {
  qrData = "WIFI:S:" + config.apid + ";T:WPA;P:" + apPassword + ";H:false;;";
  const char *qrDataChar = qrData.c_str();
  QRCode qrcoded;
  
  int qrVersion = getQrCodeVersion();
  int pixSize = getQrCodePixelSize(qrVersion);
  uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];

  qrcode_initText(&qrcoded, qrcodeData, qrVersion, 0, qrDataChar);

  epd_poweron();
  epd_clear();

  int qrWidth = pixSize * qrcoded.size;
  int qrPosX = ((EPD_WIDTH - qrWidth) / 2);
  int qrPosY = ((EPD_HEIGHT - qrWidth) / 2);
  // calculate the center of the screen
    // Serial.println(EPD_WIDTH);
    // Serial.println(qrPosX);

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

  posX = 155;
  posY = 60;
  writeln((GFXfont *)&poppins20, "No Internet connection available", &posX, &posY, framebuffer);
  posX = 150;
  posY = 490;
  writeln((GFXfont *)&poppins20, String("Connect to AP " + config.apid).c_str(), &posX, &posY, framebuffer);
  posX = 155;
  posY = 525;
  writeln((GFXfont *)&poppins20, String("With password \"" + apPassword + "\"").c_str(), &posX, &posY, framebuffer);
  draw_framebuf(true);
  epd_poweroff();

  isApScreenActive = true;
  }
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
  // Serial.println(F("qrCodeHeight pixel height is"));
  // Serial.println(qrCodeHeight);

  // Serial.println(F("Calced pixel height is"));
  // Serial.println(pixelHeight);
  return pixelHeight;
}



void draw_framebuf(bool clear_buf)
{
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    if (clear_buf)
    {
        memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    }
}


void clear_framebuf()
{
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}


void refreshScreen() {
  int32_t i = 0;
  Rect_t area = epd_full_screen();
  epd_poweron();
  delay(10);
  epd_clear();
  for (i = 0; i < 10; i++)
  {
    epd_push_pixels(area, 50, 0);
    delay(250);
  }
  epd_clear();
  for (i = 0; i < 20; i++)
  {
    epd_push_pixels(area, 50, 1);
    delay(250);
  }
  epd_clear();
  epd_poweroff();
}

char* copyString(char s[])
{
  char* s2;
  s2 = (char*)malloc(100);
 
  strcpy(s2, s);
  return (char*)s2;
}

/**
 * Get the correct linespacing for the font used
 */
uint8_t getLineSpacing(int fontSize) {
  return fontSize * 1.5;
}


void displayLastUpdateTime() {
  epd_poweron();
  int num = apiDataDoc["settings"]["requestTimestamp"];
  string temp_str=to_string(num); //converting number to a string
  char const* lastUpdateTime = temp_str.c_str(); //converting string to char Array

  int cursor_x = 20;
  int cursor_y = 530;
  clearLine(cursor_x, cursor_y);
  writeln((GFXfont *)&poppins12, lastUpdateTime, &cursor_x, &cursor_y, NULL);
  epd_poweroff();
}