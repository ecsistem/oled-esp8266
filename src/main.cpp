#include <Arduino.h>
#include <Wire.h>
#include <time.h>

#include "app_state.h"
#include "ui_screens.h"
#include "weather.h"
#include "wifi_portal.h"
#include "deauther.h"

namespace
{
  constexpr uint8_t flashButtonPin = 0;
  constexpr unsigned long flashHoldMs = 1200;
  constexpr unsigned long eyeScreenMultiplier = 2;

  bool isScreenEnabled(int idx)
  {
    if (idx == 0)
      return true;
    if (idx == 1)
      return screen1Enabled;
    if (idx == 2)
      return screen2Enabled;
    if (idx == 3)
      return screen3Enabled;
    if (idx == 4)
      return screen4Enabled;
    if (idx == 5)
      return screen5Enabled;
    return false;
  }

  int nextEnabledScreen(int current)
  {
    for (int step = 1; step <= 6; step++)
    {
      int candidate = (current + step) % 6;
      if (isScreenEnabled(candidate))
      {
        return candidate;
      }
    }

    return 0;
  }

  unsigned long currentScreenIntervalMs()
  {
    if (screen == 0)
    {
      return screenChangeIntervalMs * eyeScreenMultiplier;
    }

    return screenChangeIntervalMs;
  }

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
        if (screen == 5)
        {
          toggleDeauther();
          toggledThisPress = true;
          Serial.println(isDeauthActive() ? "Deauther started" : "Deauther stopped");
        }
        else
        {
          toggleCaptivePortalEnabled();
          toggledThisPress = true;
          Serial.println(captivePortalEnabled ? "Captive portal ativado pelo botao FLASH" : "Captive portal desativado pelo botao FLASH");
        }
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

  Wire.begin(0, 2); // SDA=D3 (GPIO0), SCL=D4 (GPIO2)
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
  updateDeauth();
  updateBeacon();
  updateProbe();

  if (!isScreenEnabled(screen))
  {
    screen = 0;
    lastScreenChange = millis();
  }

  if (millis() - lastWeatherUpdate > weatherUpdateIntervalMs)
  {
    getWeather();
    lastWeatherUpdate = millis();
  }

  if (millis() - lastScreenChange > currentScreenIntervalMs())
  {
    screen = nextEnabledScreen(screen);

    lastScreenChange = millis();
  }

  drawScreen();
}
