#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WebServer.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

extern Adafruit_SSD1306 display;
extern ESP8266WebServer server;

extern String wifiSsid;
extern String wifiPassword;
extern const char *wifiConfigPath;
extern String apSsid;
extern String apPassword;
extern bool apOpen;
extern unsigned long lastConnectAttemptAt;

extern ESP8266WebServer server;

extern float temp;

extern int screen;
extern unsigned long lastScreenChange;
extern unsigned long lastWeatherUpdate;
extern unsigned long weatherUpdateIntervalMs;
extern unsigned long screenChangeIntervalMs;
extern int timezoneOffsetHours;
extern uint8_t oledBrightness;
extern bool captivePortalEnabled;
extern bool screen1Enabled;
extern bool screen2Enabled;
extern bool screen3Enabled;
extern bool screen4Enabled;
extern bool screen5Enabled;

extern String deautherApMac;
extern String deautherClientMac;
extern int deautherChannel;
extern const unsigned long angryHoldMs;
extern unsigned long angryUntil;
extern const unsigned long evilHoldMs;
extern unsigned long evilUntil;
extern String portalToastMessage;
extern unsigned long portalToastUntil;
