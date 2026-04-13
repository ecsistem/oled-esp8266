#include <Arduino.h>
#include <Wire.h>
#include <time.h>

#include "app_state.h"
#include "ui_screens.h"
#include "weather.h"
#include "wifi_portal.h"

namespace
{
  constexpr uint8_t flashButtonPin = 0;
  constexpr unsigned long flashHoldMs = 1200;

  void handleFlashButton()
  {
    static unsigned long pressedAt = 0;
    static bool toggledThisPress = false;

    bool pressed = digitalRead(flashButtonPin) == LOW;
    unsigned long now = millis();

    if (pressed)
    {
      if (pressedAt == 0)
      {
        pressedAt = now;
        toggledThisPress = false;
      }

      if (!toggledThisPress && now - pressedAt >= flashHoldMs)
      {
        toggleCaptivePortalEnabled();
        toggledThisPress = true;
        Serial.println(captivePortalEnabled ? "Captive portal ativado pelo botao FLASH" : "Captive portal desativado pelo botao FLASH");
      }
    }
    else
    {
      pressedAt = 0;
      toggledThisPress = false;
    }
  }
}

void setup()
{
  Serial.begin(115200);

  Wire.begin(14, 12);
  pinMode(flashButtonPin, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    while (true)
      ;
  }

  initWiFiAndPortal();

  configTime(timezoneOffsetHours * 3600, 0, "pool.ntp.org");
  randomSeed(micros());

  getWeather();
}

void loop()
{
  handlePortalClient();
  handleFlashButton();

  if (millis() - lastWeatherUpdate > weatherUpdateIntervalMs)
  {
    getWeather();
    lastWeatherUpdate = millis();
  }

  if (millis() - lastScreenChange > screenChangeIntervalMs)
  {
    screen++;
    if (screen > 4)
    {
      screen = 0;
    }

    lastScreenChange = millis();
  }

  drawScreen();
}
