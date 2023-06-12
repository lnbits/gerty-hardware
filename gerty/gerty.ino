#include <Arduino.h>
#include <ArduinoJson.h>
#include <AutoConnect.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <WebServer.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "access_point.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_adc_cal.h"
#include <esp_sleep.h>
#include <esp_task_wdt.h>

#include "epd_driver.h"

#include "anonpro12.h"
#include "anonpro15.h"
#include "anonpro20.h"
#include "anonpro40.h"
#include "anonpro80.h"
#include "gear.h"
#include "qrcoded.h"
#include "sleep_eye.h"
#include "smile.h"

WebServer server;
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
#define BATT_PIN            14
#define SD_MISO             16
#define SD_MOSI             15
#define SD_SCLK             11
#define SD_CS               15
#define BUTTON_PIN_BITMASK GPIO_SEL_21

String spiffing;
String apPassword = "ToTheMoon1"; //default WiFi AP password
String gertyEndpoint = "https://legend.lnbits.com/gerty/api/v1/gerty/pages/aW57DPt2PBSZPrF5PYAaxL";
String qrData;

bool gertyApiExists = true;

uint8_t *framebuffer;
int vref = 1100;

int isFirstLine = true;
int sleepTime = 300; // The time to sleep in seconds
int showTextBoundRect = false;
StaticJsonDocument<1500> apiDataDoc; 
String selection;

uint16_t AVAILABLE_EPD_HEIGHT = EPD_HEIGHT - 40;

int textBoxStartX = 0;
int textBoxStartY = 0;
int lineSpacing = 100;
int firstLineOffset = 40;
int totalTextWidth = 0;
int totalTextHeight = 0;

int posX = 0;
int posY = 0;
int fontSize;

enum alignment
{
    LEFT,
    RIGHT,
    CENTER
};

//21, 34, 35, 39
int portalPin = 21;
int triggerAp = false;
const int buttonPin = 21;     // the pin number of the button

void setup()
{
  char buf[128];
  Serial.begin(115200);
  Serial.println("Im awake");

  //save some battery here
  btStop();
  pinMode(buttonPin, INPUT);
  epd_init();
  showGear();

  FlashFS.begin(FORMAT_ON_FAIL);
  SPIFFS.begin(true);
   
  int buttonState = digitalRead(buttonPin);
  Serial.println(F("button pin value is"));
  Serial.println(buttonState);
  if (buttonState != HIGH) {
      Serial.println("Launch portal");
      triggerAp = true;
  } else {
    Serial.println("Button state is low. Dont auto-launch portal.");
  }

  framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
  if (!framebuffer) {
      Serial.println("alloc memory failed !!!");
      while (1);
  }
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
  epd_poweroff_all();
  initWiFi();
}

void loop()
{
  isFirstLine = true;
  int screenToDisplay = 0;
  screenToDisplay = loadScreenNumberToDisplay();
  loadSettings();
  getData(screenToDisplay);

  if(!gertyApiExists) {
    showGertyDoesNotExistScreen();
    sleepTime = 3600;
  } else {
    loadSettingsFromApi();

    int sleepTimeThreshold = 21600;

    if(sleepTime >= sleepTimeThreshold) {
      showSleeping();
    }
    else {
      displayData();
    }
    displayNextUpdateTime();
  }

  displayVoltage();
  delay(500);
  hibernate(sleepTime);
}

/**
 * @brief Show the splash screen
 * 
 */
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
  epd_poweroff_all();
}

/**
 * @brief Get the screen number to display
 * 
 * @return int 
 */
int loadScreenNumberToDisplay() {
  File file = SPIFFS.open("/config.txt");
  spiffing = file.readStringUntil('\n');
  String tempScreenToDisplay = spiffing.c_str();
  int tempScreenToDisplayInt = tempScreenToDisplay.toInt();
  file.close();
  return tempScreenToDisplayInt;
}

void saveNextScreenToDisplay(int screenToDisplay) {
  File configFile = SPIFFS.open("/config.txt", "w");
  configFile.print(String(screenToDisplay));
  configFile.close();
}

/**
 * @brief Set up the WiFi connection
 * 
 */
void initWiFi() {
  configureAccessPoint();
    
  WiFi.mode(WIFI_STA);
  while (WiFi.status() != WL_CONNECTED) {
    delay(3000);
  }
}

/**
 * @brief While captive portal callback
 * 
 * @return true 
 * @return false 
 */
bool whileCP(void) {
  bool rc;
  showAPLaunchScreen();
  rc = true;
  return rc;
}

/**
 * @brief Configure the WiFi AP
 * 
 */
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

  config.autoReconnect = true;
  config.reconnectInterval = 1; // 30s
  config.beginTimeout = 30000UL;

  config.immediateStart = triggerAp;
  config.hostName = "Gerty";
  config.apid = "Gerty-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  config.apip = IPAddress(6, 15, 6, 15);      // Sets SoftAP IP address
  config.gateway = IPAddress(6, 15, 6, 15);     // Sets WLAN router IP address
  config.psk = apPassword;
  config.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_OPENSSIDS | AC_MENUITEM_RESET;
  config.title = "Gerty";
  config.portalTimeout = 120000;

  portal.whileCaptivePortal(whileCP);

  portal.join({elementsAux, saveAux});
  portal.config(config);

    // Establish a connection with an autoReconnect option.
  if (portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  } else {
    // Restart here
    showNoWifiConnectionScreen();
    hibernate(300);
  }
}

WiFiClientSecure client;

/**
 * @brief This gets the JSON data from the LNbits endpoint and serialises it into
 * memory to be used by the microcontroller
 * 
 * @param screenToDisplay 
 */
void getData(int screenToDisplay) {
    HTTPClient http;
    client.setInsecure();

    const char * headerKeys[] = {"date"} ;
    const size_t numberOfHeaders = 1;

    const String gertyEndpointWithScreenNumber = gertyEndpoint + "/" + screenToDisplay;
    Serial.println("Getting data from " + gertyEndpointWithScreenNumber);
    // Send request
    http.begin(client, gertyEndpointWithScreenNumber);
    http.collectHeaders(headerKeys, numberOfHeaders);
    http.GET();

    String responseDate = http.header("date");

    // Print the response
    String data = http.getString();

    DeserializationError error = deserializeJson(apiDataDoc, data);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    // Display the Gerty does not exist screen if does not exist
    if(apiDataDoc["detail"] == "Gerty does not exist.") {
      gertyApiExists = false;
    }
    // Disconnect
    http.end();
}

void loadSettingsFromApi() {
  sleepTime = apiDataDoc["settings"]["refreshTime"];
  showTextBoundRect = apiDataDoc["settings"]["showTextBoundRect"];
}

/**
 * @brief Display the data for the specified screen 
 * 
 */
void displayData() {
  epd_poweron();
  epd_clear();
  //get settings
  int nextScreen = apiDataDoc["settings"]["nextScreenNumber"];

  saveNextScreenToDisplay(nextScreen);

  const char* slug = apiDataDoc["screen"]["slug"]; 

  const char* group = apiDataDoc["screen"]["group"]; 

  if(apiDataDoc["screen"]["title"]) {
    int textBoundsEndX = 0;
    int textBoundsEndY = 0;
    int textBoundsWidth = 0;
    int textBoundsHeight = 0;
    posX = 0;
    posY = 0;

    const char *title = apiDataDoc["screen"]["title"]; 

    get_text_bounds((GFXfont *)&anonpro20, title, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
    // write the text in the middle
    posX = (EPD_WIDTH - textBoundsWidth) / 2;
    posY = 60;
    writeln((GFXfont *)&anonpro20, title, &posX, &posY, NULL);
  }
    
  uint16_t areaCount = apiDataDoc["screen"]["areas"].size();
  uint16_t currentAreaIndex = 0;
  for (JsonArray areaElems : apiDataDoc["screen"]["areas"].as<JsonArray>()) {
    char json_string[256];
    isFirstLine = true;

    setTextBoxCoordinates(areaElems, areaCount, currentAreaIndex);

    posY = 0;
    isFirstLine = true;
    for (JsonObject textElem : areaElems) {
      renderText(textElem);
      isFirstLine = false;
    }
    draw_framebuf(true);
    ++currentAreaIndex;
  }
  epd_poweroff_all();
}

/**
 * @brief Set the textBoxStartX and textBoxStartY coordinates to allow the content to be centred
 * 
 * @param textElem 
 */
void renderText(JsonObject textElem) {
  const char* value = textElem["value"]; 

  fontSize = textElem["size"]; 

  const char* pos = textElem["position"];

  if(textElem["x"] && textElem["y"]) {
    posX = textElem["x"];
    posY = textElem["y"];
  } else {
    std::string s = textElem["value"];

    if (s.find('\n') != std::string::npos) {
      posX = textBoxStartX;
    }
    else {
      int tbPosX = 0;
      int tbPosY = 0;
      int textBoundsEndX = 0;
      int textBoundsEndY = 0;
      int textBoundsWidth = 0;
      int textBoundsHeight = 0;

      switch(fontSize) {
        case 12:
          get_text_bounds((GFXfont *)&anonpro12, (char *)value, &tbPosX, &tbPosY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 15:
          get_text_bounds((GFXfont *)&anonpro15, (char *)value, &tbPosX, &tbPosY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 20:
          get_text_bounds((GFXfont *)&anonpro20, (char *)value, &tbPosX, &tbPosY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 40:
          get_text_bounds((GFXfont *)&anonpro40, (char *)value, &tbPosX, &tbPosY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 80:
          get_text_bounds((GFXfont *)&anonpro80, (char *)value, &tbPosX, &tbPosY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        default:
          get_text_bounds((GFXfont *)&anonpro20, (char *)value, &tbPosX, &tbPosY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
      }
      // posX = (EPD_WIDTH - textBoundsWidth) / 2;   
      posX = textBoxStartX + (totalTextWidth - textBoundsWidth) / 2;
    }

    // initialise the text box starting position if it hasnt been set
    if(posY == 0) {
      posY = textBoxStartY + firstLineOffset;
    }
    // add a line spacing if this isnt the first element
    if(!isFirstLine) {
      posY += getLineSpacing(fontSize);
    }
  }

  switch(fontSize) {
    case 12:
      write_string((GFXfont *)&anonpro12, (char *)value, &posX, &posY, framebuffer);
      break;
    case 15:
      write_string((GFXfont *)&anonpro15, (char *)value, &posX, &posY, framebuffer);
      break;
    case 20:
      write_string((GFXfont *)&anonpro20, (char *)value, &posX, &posY, framebuffer);
      break;
    case 40:
      write_string((GFXfont *)&anonpro40, (char *)value, &posX, &posY, framebuffer);
      break;
    case 80:
      write_string((GFXfont *)&anonpro80, (char *)value, &posX, &posY, framebuffer);
      break;
    default:
      write_string((GFXfont *)&anonpro20, (char *)value, &posX, &posY, framebuffer);
  }

  isFirstLine = false;
  
}

/**
 * @brief Set the textBoxStartX and textBoxStartY coordinates to allow the content to be centred
 * 
 * @param textElems 
 * @param areaCount 
 * @param currentAreaIndex 
 */
void setTextBoxCoordinates(JsonArray textElems, uint16_t areaCount, uint16_t currentAreaIndex) {
  totalTextHeight = 0;
  totalTextWidth = 0;
  int posY = 0;
  int posX = 0;
  int endPosX = 0;
  int endPosY = 0;

  char* stringToSplit;
  char delimiter[] = "\n";
  char* ptr;

  isFirstLine = true;
  // for each text element in JSON array
  for (JsonObject textElem : textElems) {
    posX = 0;

    char* value = strdup(textElem["value"]);

    int textWidth = 0;
    int textHeight = 0;
    int endY = 0;
    int endX = 0;
    int textBoxWidth = 0;
    int textBoxHeight = 0;

    fontSize = textElem["size"];

    // initialize first part (string, delimiter)
    char * ptr;
    ptr = strtok(value, "\n");

    int textBoundsEndX = 0;
    int textBoundsEndY = 0;
    int textBoundsWidth = 0;
    int textBoundsHeight = 0;

    while(ptr != NULL) {
      switch(fontSize) {
        case 12:
          get_text_bounds((GFXfont *)&anonpro12, ptr, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 15:
          get_text_bounds((GFXfont *)&anonpro15, ptr, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 20:
          get_text_bounds((GFXfont *)&anonpro20, ptr, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 40:
          get_text_bounds((GFXfont *)&anonpro40, ptr, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        case 80:
          get_text_bounds((GFXfont *)&anonpro80, ptr, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
          break;
        default:
          get_text_bounds((GFXfont *)&anonpro20, ptr, &posX, &posY, &textBoundsEndX, &textBoundsEndY, &textBoundsWidth, &textBoundsHeight, NULL);
      }

      if(textBoundsWidth > totalTextWidth) {
        totalTextWidth = textBoundsWidth;
      }

      ptr = strtok(NULL, delimiter);
    }

    value = strdup(textElem["value"]);
    
    int posYBefore = posY;
    
    switch(fontSize) {
      case 12:
        write_string((GFXfont *)&anonpro12, (char *)value, &posX, &posY, framebuffer);
        break;
      case 15:
        write_string((GFXfont *)&anonpro15, (char *)value, &posX, &posY, framebuffer);
        break;
      case 20:
        write_string((GFXfont *)&anonpro20, (char *)value, &posX, &posY, framebuffer);
        break;
      case 40:
        write_string((GFXfont *)&anonpro40, (char *)value, &posX, &posY, framebuffer);
        break;
      case 80:
        write_string((GFXfont *)&anonpro80, (char *)value, &posX, &posY, framebuffer);
        break;
      default:
        write_string((GFXfont *)&anonpro20, (char *)value, &posX, &posY, framebuffer);
    }
    totalTextHeight += posY - posYBefore;
    if(!isFirstLine) {
      totalTextHeight += getLineSpacing(fontSize);
    }

    // set starting X and Y coordinates for all text based on current area index and total area count
    if(areaCount == 4 && currentAreaIndex == 0) {
      textBoxStartX = ((EPD_WIDTH / 2 - totalTextWidth) / 2);
      textBoxStartY = ((AVAILABLE_EPD_HEIGHT / 2 - totalTextHeight) / 2);
    }
    else if(areaCount == 4 && currentAreaIndex == 1) {
      textBoxStartX = ((EPD_WIDTH / 2 - totalTextWidth) / 2) + EPD_WIDTH / 2;
      textBoxStartY = ((AVAILABLE_EPD_HEIGHT / 2 - totalTextHeight) / 2);
    }
    else if(areaCount == 4 && currentAreaIndex == 2) {
      textBoxStartX = ((EPD_WIDTH / 2 - totalTextWidth) / 2);
      textBoxStartY = ((AVAILABLE_EPD_HEIGHT / 2 - totalTextHeight) / 2)  + AVAILABLE_EPD_HEIGHT / 2;
    }
    else if(areaCount == 4 && currentAreaIndex == 3) {
      textBoxStartX = ((EPD_WIDTH / 2 - totalTextWidth) / 2) + EPD_WIDTH / 2;
      textBoxStartY = ((AVAILABLE_EPD_HEIGHT / 2 - totalTextHeight) / 2)  + AVAILABLE_EPD_HEIGHT / 2;
    }
    else {
      textBoxStartX = (EPD_WIDTH - totalTextWidth) / 2;
      textBoxStartY = (AVAILABLE_EPD_HEIGHT - totalTextHeight) / 2;
    }

    if(textBoxStartX < 0) {
      textBoxStartX = 10;
    }
    
    if(textBoxStartY < 0) {
      textBoxStartY = 10;
    }
  }

  clear_framebuf();

  if(showTextBoundRect) {
    // epd_draw_rect(textBoxStartX, textBoxStartY, totalTextWidth, totalTextHeight, 0, framebuffer);
  }
}

/**
 * @brief Show the current battery voltage
 * 
 */
void displayVoltage() {
    // When reading the battery voltage, POWER_EN must be turned on
    epd_poweron();
    delay(10); // Make adc measurement more accurate
    uint16_t v = analogRead(BATT_PIN);
    float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    String voltage = String(battery_voltage) + "V";

    int cursor_x = 880;
    int cursor_y = 530;
    // clearLine(cursor_x, cursor_y);
    writeln((GFXfont *)&anonpro12, (char *)voltage.c_str(), &cursor_x, &cursor_y, NULL);
    epd_poweroff_all();
}

/**
 * @brief Clear a line on the EPD with the given coordinates
 * 
 * @param xPos 
 * @param yPos 
 */
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
    int qrPosY = 110;

    // calculate the center of the screen
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

    posX = EPD_WIDTH / 2;
    posY = 35;
    draw_str(anonpro20, "No Internet connection available", posX, posY, CENTER);

    posY = 415;
    draw_str(anonpro20, String("Connect to AP " + config.apid).c_str(), posX, posY, CENTER);
    
    posY = 470;
    draw_str(anonpro20, String("With password \"" + apPassword + "\"").c_str(), posX, posY, CENTER);

    draw_framebuf(true);
    epd_poweroff_all();

    isApScreenActive = true;
  }
}

/**
 * @brief Show does not exist screen
 * 
 */
void showGertyDoesNotExistScreen() {
  epd_poweron();
  epd_clear();

  int x_pos = (int)(EPD_WIDTH * 0.5f);
  int y_pos = 50;

  draw_str(anonpro40, "Whoops :(", x_pos, y_pos, CENTER);

  y_pos = (int)(EPD_HEIGHT * 0.45f);
  draw_str(anonpro20, "I could not find your Gerty.", x_pos, y_pos, CENTER);

  y_pos = (int)(EPD_HEIGHT * 0.66f);
  draw_str(anonpro15, "Please reboot this device into AP mode and check", x_pos, y_pos, CENTER);
  y_pos += 30;
  draw_str(anonpro15, "that your Gerty API URL value is correct.", x_pos, y_pos, CENTER);

  draw_framebuf(true);

  epd_poweroff_all();
}

/**
 * @brief Show the "thinking" gear icon on the screen
 * 
 */
void showGear() {
    epd_poweron();
    Rect_t area = {
        .x = 10,
        .y = 10,
        .width = gear_width,
        .height = gear_height,
    };
    epd_draw_grayscale_image(area, (uint8_t *)gear_data);
    delay(250);
    epd_poweroff_all();
}

/**
 * @brief Show the no wifi screen to the user
 * 
 */
void showNoWifiConnectionScreen() {
  epd_poweron();
  epd_clear();

  int x_pos = (int)(EPD_WIDTH * 0.5f);
  int y_pos = 50;

  draw_str(anonpro40, "Oh dear :(", x_pos, y_pos, CENTER);

  y_pos = (int)(EPD_HEIGHT * 0.45f);
  draw_str(anonpro20, "I could not connect to the Internet.", x_pos, y_pos, CENTER);

  y_pos = (int)(EPD_HEIGHT * 0.66f);
  draw_str(anonpro15, "Please reboot this device into AP mode", x_pos, y_pos, CENTER);
  y_pos += 30;
  draw_str(anonpro15, "and configure a WiFi connection.", x_pos, y_pos, CENTER);

  y_pos += 60;
  draw_str(anonpro12, "Retrying every 5 minutes", x_pos, y_pos, CENTER);

  draw_framebuf(true);

  epd_poweroff_all();
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

  return pixelHeight;
}

/**
 * @brief Draw the current framebuffer to the EPD
 * 
 * @param clear_buf 
 */
void draw_framebuf(bool clear_buf)
{
  epd_draw_grayscale_image(epd_full_screen(), framebuffer);
  if (clear_buf)
  {
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
  }
}

/**
 * @brief Clear the EPD framebuffer
 * 
 */
void clear_framebuf()
{
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

/**
 * @brief Draw a string to the screen positioned according to "align" on the x, y coords
 * 
 * @param font 
 * @param txt 
 * @param x 
 * @param y 
 * @param align 
 */
void draw_str(const GFXfont font, const String txt, int x, int y, alignment align)
{
  const char *string = (char *)txt.c_str();
  int x1, y1;
  int w, h;
  int xx = x, yy = y;
  get_text_bounds(&font, string, &xx, &yy, &x1, &y1, &w, &h, NULL);
  if (align == RIGHT)
    x = x - w;
  if (align == CENTER)
    x = x - w / 2;
  int cursor_y = y + h;
  writeln(&font, string, &x, &cursor_y, framebuffer);
}

/**
 * @brief Run a big screen refresh cycle
 * 
 */
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
  epd_poweroff_all();
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
  switch (fontSize)
  {
    default:
      return fontSize * 1.5;
      break;
  }
}

/**
 * @brief Display time of next update on the EPD
 * 
 */
void displayNextUpdateTime() {
  epd_poweron();
  const char * requestTime = apiDataDoc["settings"]["requestTimestamp"];
  int cursor_x = 20;
  int cursor_y = 530;
  clearLine(cursor_x, cursor_y);
  writeln((GFXfont *)&anonpro12, requestTime, &cursor_x, &cursor_y, framebuffer);
  draw_framebuf(true);
  epd_poweroff_all();
}

/**
 * @brief Show the "Sleeping" screen
 * 
 */
void showSleeping() {
  epd_poweron();

  epd_clear();
  Rect_t eye_l = {
    .x = 300,
    .y = 150,
    .width = sleep_eye_width,
    .height = sleep_eye_height,
  };

  Rect_t eye_r = {
    .x = 540,
    .y = 150,
    .width = sleep_eye_width,
    .height = sleep_eye_height,
  };
  
  epd_copy_to_framebuffer(eye_l, (uint8_t *)sleep_eye_data, framebuffer);
  epd_copy_to_framebuffer(eye_r, (uint8_t *)sleep_eye_data, framebuffer);
  epd_fill_circle(483, 333, 50, 0, framebuffer);
  draw_framebuf(true);

  delay(1000);
  epd_poweroff_all();
}

/**
 * @brief Deep sleep the device for a period of time
 * 
 * @param sleepTimeSeconds The time to deep sleep in seconds
 */
void hibernate(int sleepTimeSeconds) {

int buttonState = digitalRead(buttonPin);
  // If the button is pressed (because it's pulled up, pressing will connect it to GND and read LOW).
  if (buttonState == LOW) {
    Serial.println("Button state is low (pressed)");
    delay(500); // Debouncing delay to avoid multiple detections for one single press.
  } else {
    Serial.println("Button state is  high (not pressed)");
  }

  
  uint64_t deepSleepTime = (uint64_t)sleepTimeSeconds * (uint64_t)1000 * (uint64_t)1000;
  Serial.println("Going to sleep for seconds");
  Serial.println(deepSleepTime);
  Serial.println("PIN is");
  Serial.println(GPIO_NUM_21);


//  esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 1); //1 = High, 0 = Low
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_sleep_enable_timer_wakeup(deepSleepTime);
  esp_deep_sleep_start();
}
