#include "ui_screens.h"

#include <time.h>
#include <ESP8266WiFi.h>

#include "app_state.h"
#include "eye_animation.h"

void drawScreen()
{
  display.clearDisplay();
  display.setTextColor(WHITE);

  if (screen == 0)
  {
    updateEyeAnimation();
    drawEyeAnimation();
  }
  else if (screen == 1)
  {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(WiFi.SSID());

    display.setCursor(0, 20);
    display.print("STA: ");
    display.println(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");

    display.setCursor(0, 35);
    display.print("AP:");
    display.println(WiFi.softAPIP());

    display.setCursor(0, 50);
    if (WiFi.status() == WL_CONNECTED)
    {
      display.print("RSSI:");
      display.print(WiFi.RSSI());
      display.println(" dBm");
    }
    else
    {
      display.println("Abra: 192.168.4.1");
    }
  }
  else if (screen == 2)
  {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("TEMPERATURA");

    display.setTextSize(3);
    display.setCursor(0, 25);
    display.print(temp);
    display.println("C");
  }
  else if (screen == 3)
  {
    struct tm timeinfo;

    if (getLocalTime(&timeinfo))
    {
      display.setTextSize(3);
      display.setCursor(2, 14);
      display.printf("%02d:%02d",
                     timeinfo.tm_hour,
                     timeinfo.tm_min);

      display.setTextSize(1);
      display.setCursor(98, 24);
      display.printf("%02d", timeinfo.tm_sec);
    }
    else
    {
      display.setTextSize(2);
      display.setCursor(10, 24);
      display.println("Sem hora");
    }
  }

  display.display();
}
