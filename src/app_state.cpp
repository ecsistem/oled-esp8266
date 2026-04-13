#include "app_state.h"

#include <Wire.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String wifiSsid = "OLIVEIRA";
String wifiPassword = "residencial242";
const char *wifiConfigPath = "/wifi.json";
String apSsid;
String apPassword = "12345678";
bool apOpen = false;
unsigned long lastConnectAttemptAt = 0;

ESP8266WebServer server(80);

float temp = 0;

int screen = 0;
unsigned long lastScreenChange = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long weatherUpdateIntervalMs = 60000;
unsigned long screenChangeIntervalMs = 8000;
int timezoneOffsetHours = -3;
uint8_t oledBrightness = 180;
bool captivePortalEnabled = false;

unsigned long angryUntil = 0;
const unsigned long angryHoldMs = 5000;
