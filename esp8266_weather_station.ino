#include <FS.h>
#include <EEPROM.h>
#include <Wire.h>
#include <time.h>
#include <Ticker.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <JsonListener.h>
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>

// * Include settings
#include "settings.h"
#include "token.h"

#include <simpleDSTadjust.h>
#include <WundergroundClient.h>
#include "Fonts/WeatherStationFonts.h"
#include "Fonts/WeatherStationImages.h"
#include "Fonts/DSEG7Classic-BoldFont.h"

// * Initiate led blinker library
Ticker ticker;

// * Initiate Watchdog
Ticker tickerOSWatch;

// * Initiate WIFI client
WiFiClient espClient;

// * Initiate display
SSD1306Wire display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);

// * Initiate Menu
OLEDDisplayUi ui( &display );

// * Initiate time library
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600}; // * Central European Summer Time = UTC/GMT +2 hours
struct dstRule EndRule   = {"CET", Last, Sun, Oct, 2, 0};     // * Central European Time = UTC/GMT +1 hour

simpleDSTadjust dstAdjusted(StartRule, EndRule);

// Initialize Wunderground client with METRIC setting
WundergroundClient wunderground(IS_METRIC);

// * Last update value
String lastUpdate = "--";

// * declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void drawOtaProgress(unsigned int, unsigned int);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();
int8_t getWifiQuality();

FrameCallback frames[] = { drawDateTime, drawCurrentWeather, drawForecast, drawForecast2  };
int numberOfFrames = 4;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

// **********************************
// * System Functions               *
// **********************************

// * Watchdog function
void ICACHE_RAM_ATTR osWatch(void)
{
    unsigned long t = millis();
    unsigned long last_run = abs(t - last_loop);
    if (last_run >= (OSWATCH_RESET_TIME * 1000)) {
        // save the hit here to eeprom or to rtc memory if needed
        ESP.restart();  // normal reboot
    }
}

// * Gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());

    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 10, "WIFI Manager");
    display.drawString(64, 20, "Please connect to AP");
    display.drawString(64, 30, myWiFiManager->getConfigPortalSSID());
    display.drawString(64, 40, "To finish WIFI Configuration");
    display.display();

    // * Entered config mode, make led toggle faster
    ticker.attach(0.2, tick);
}

// * Callback notifying us of the need to save config
void save_wifi_config_callback ()
{
    shouldSaveConfig = true;
}

// * Blink on-board Led
void tick()
{
    // * Toggle state
    int state = digitalRead(LED_BUILTIN);    // * Get the current state of GPIO1 pin
    digitalWrite(LED_BUILTIN, !state);       // * Set pin to the opposite state
}

// **********************************
// * OTA helpers                    *
// **********************************

// * Setup update over the air
void setup_ota()
{
    Serial.println(F("Arduino OTA activated."));

    // * Port defaults to 8266
    ArduinoOTA.setPort(8266);

    // * Set hostname for OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        Serial.println(F("Arduino OTA: Start"));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println(F("Arduino OTA: End (Running reboot)"));
    });

    ArduinoOTA.onProgress(drawOtaProgress);

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Arduino OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println(F("Arduino OTA: Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            Serial.println(F("Arduino OTA: Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            Serial.println(F("Arduino OTA: Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println(F("Arduino OTA: Receive Failed"));
        else if (error == OTA_END_ERROR)
            Serial.println(F("Arduino OTA: End Failed"));
    });
    ArduinoOTA.begin();
    Serial.println(F("Arduino OTA finished"));
}

// **********************************
// * UI                             *
// **********************************

void setup_ui()
{
    // * Setup frame display time to 10 sec
    ui.setTargetFPS(30);
    ui.setTimePerFrame(10*1000);

    // * Hack until disableIndicator works: Set an empty symbol
    ui.setActiveSymbol(emptySymbol);
    ui.setInactiveSymbol(emptySymbol);
    ui.disableIndicator();

    // * You can change the transition that is used: [SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN]
    ui.setFrameAnimation(SLIDE_LEFT);
    ui.setFrames(frames, numberOfFrames);
    ui.setOverlays(overlays, numberOfOverlays);
}

// **********************************
// * Setup                          *
// **********************************

void setup()
{
    // Configure Watchdog
    last_loop = millis();
    tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), osWatch);

    // * Configure Serial and EEPROM
    Serial.begin(BAUD_RATE);

    // * Initiate EEPROM
    EEPROM.begin(512);

    // * Set led pin as output
    pinMode(LED_BUILTIN, OUTPUT);

    // * Start ticker with 0.5 because we start in AP mode and try to connect
    ticker.attach(0.6, tick);

    // * Initialize display
    display.init();
    display.clear();
    display.display();

    // display.flipScreenVertically();  // Comment out to flip display 180deg
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);

    // * Print WIFI logo while connecting to AP
    display.drawXbm(-6, 5, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
    display.drawString(88, 18, "Weather Station");
    display.display();

    // * WiFiManager local initialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // * Reset settings - uncomment for testing
    //   wifiManager.resetSettings();

    // * Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // * Set timeout
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);

    // * Set save config callback
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    // * Fetches SSID and pass and tries to connect
    // * Reset when no connection after 10 seconds
    if (!wifiManager.autoConnect()) {
        Serial.println(F("Failed to connect to WIFI and hit timeout"));
        // * Reset and try again
        ESP.reset();
        delay(WIFI_TIMEOUT);
    }

    // * If you get here you have connected to the WiFi
    Serial.println(F("Connected to WIFI..."));

    // * Keep LED on
    ticker.detach();
    digitalWrite(LED_BUILTIN, LOW);

    // * Configure OTA
    setup_ota();

    // * Startup MDNS Service
    Serial.println(F("Starting MDNS responder service"));
    MDNS.begin(HOSTNAME);

    // * Configure ui
    setup_ui();

    // * First weather data fetch
    updateData(&display);

    // * Set ticker callback for updating weather data
    ticker.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
}


// **********************************
// * Loop                           *
// **********************************

void loop()
{
    // * Update last loop watchdog value
    last_loop = millis();

    // * Accept ota requests if offered
    ArduinoOTA.handle();

    if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
      updateData(&display);
    }

    int remainingTimeBudget = ui.update();

    if (remainingTimeBudget > 0) {
      ArduinoOTA.handle();
      delay(remainingTimeBudget);
    }
}

// **********************************
// * Weather functions              *
// **********************************

void drawProgress(OLEDDisplay *display, int percentage, String label)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_10);
    display->drawString(64, 10, label);
    display->drawProgressBar(2, 28, 124, 12, percentage);
    display->display();
}

void drawOtaProgress(unsigned int progress, unsigned int total)
{
    Serial.printf("Arduino OTA Progress: %u%%\r", (progress / (total / 100)));

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 10, "OTA Update");
    display.drawProgressBar(2, 28, 124, 12, progress / (total / 100));
    display.display();
}

void updateData(OLEDDisplay *display)
{
    drawProgress(display, 10, "Updating time...");
    configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);

    drawProgress(display, 30, "Updating conditions...");
    wunderground.updateConditions(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);

    drawProgress(display, 50, "Updating forecasts...");
    wunderground.updateForecast(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);

    delay(500);

    readyForWeatherUpdate = false;
    drawProgress(display, 100, "Done...");

    delay(1000);
}

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    char *dstAbbrev;
    char time_str[11];
    time_t now = dstAdjusted.time(&dstAbbrev);
    struct tm * timeinfo = localtime (&now);

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_10);
    String date = ctime(&now);
    date = date.substring(0,11) + String(1900+timeinfo->tm_year);
    int textWidth = display->getStringWidth(date);
    display->drawString(64 + x, 5 + y, date);
    display->setFont(DSEG7_Classic_Bold_21);
    display->setTextAlignment(TEXT_ALIGN_RIGHT);

    sprintf(time_str, "%02d:%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    display->drawString(108 + x, 19 + y, time_str);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);

    sprintf(time_str, "%s", dstAbbrev);
    display->drawString(108 + x, 27 + y, time_str);  // Known bug: Cuts off 4th character of timezone abbreviation

}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(60 + x, 5 + y, wunderground.getWeatherText());

    display->setFont(ArialMT_Plain_24);
    String temp = wunderground.getCurrentTemp() + (IS_METRIC ? "°C": "°F");

    display->drawString(60 + x, 15 + y, temp);
    int tempWidth = display->getStringWidth(temp);

    display->setFont(Meteocons_Plain_42);
    String weatherIcon = wunderground.getTodayIcon();
    int weatherIconWidth = display->getStringWidth(weatherIcon);
    display->drawString(32 + x - weatherIconWidth / 2, 05 + y, weatherIcon);
}

void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    drawForecastDetails(display, x, y, 0);
    drawForecastDetails(display, x + 44, y, 2);
    drawForecastDetails(display, x + 88, y, 4);
}


void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    drawForecastDetails(display, x, y, 6);
    drawForecastDetails(display, x + 44, y, 8);
    drawForecastDetails(display, x + 88, y, 10);
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_10);
    String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
    day.toUpperCase();
    display->drawString(x + 20, y, day);

    display->setFont(Meteocons_Plain_21);
    display->drawString(x + 20, y + 12, wunderground.getForecastIcon(dayIndex));

    display->setFont(ArialMT_Plain_10);
    display->drawString(x + 20, y + 34, wunderground.getForecastLowTemp(dayIndex) + "|" + wunderground.getForecastHighTemp(dayIndex));
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state)
{
    char time_str[11];
    time_t now = dstAdjusted.time(nullptr);
    struct tm * timeinfo = localtime (&now);

    display->setFont(ArialMT_Plain_10);

    sprintf(time_str, "%02d:%02d:%02d\n",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(5, 52, time_str);

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    String temp = wunderground.getCurrentTemp() + (IS_METRIC ? "°C": "°F");
    display->drawString(101, 52, temp);

    int8_t quality = getWifiQuality();
    for (int8_t i = 0; i < 4; i++) {
      for (int8_t j = 0; j < 2 * (i + 1); j++) {
        if (quality > i * 25 || j == 0) {
          display->setPixel(120 + 2 * i, 61 - j);
        }
      }
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(Meteocons_Plain_10);

    String weatherIcon = wunderground.getTodayIcon();
    int weatherIconWidth = display->getStringWidth(weatherIcon);

    // display->drawString(64, 55, weatherIcon);
    display->drawString(77, 53, weatherIcon);
    display->drawHorizontalLine(0, 51, 128);
}

// * Converts the dBm to a range between 0 and 100%
int8_t getWifiQuality()
{
    int32_t dbm = WiFi.RSSI();
    if(dbm <= -100) {
        return 0;
    } else if(dbm >= -50) {
        return 100;
    } else {
        return 2 * (dbm + 100);
    }
}

void setReadyForWeatherUpdate()
{
    Serial.println("Setting readyForUpdate to true");
    readyForWeatherUpdate = true;
}
