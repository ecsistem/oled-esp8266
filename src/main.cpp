#include <Arduino.h>
#include <Wire.h>
#include <time.h>

#include "app_state.h"
#include "ui_screens.h"
#include "weather.h"
#include "wifi_portal.h"

void setup()
{
  Serial.begin(115200);

  Wire.begin(14, 12);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    while (true)
      ;
  }

  initWiFiAndPortal();

  configTime(-3 * 3600, 0, "pool.ntp.org");
  randomSeed(micros());

  getWeather();
}

void loop()
{
  handlePortalClient();

  if (millis() - lastWeatherUpdate > 60000)
  {
    getWeather();
    lastWeatherUpdate = millis();
  }

  if (millis() - lastScreenChange > 8000)
  {
    screen++;
    if (screen > 3)
    {
      screen = 0;
    }

    lastScreenChange = millis();
  }

  drawScreen();
}
