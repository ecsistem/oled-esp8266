#include "ui_screens.h"

#include <time.h>
#include <ESP8266WiFi.h>

#include "app_state.h"
#include "eye_animation.h"
#include "wifi_scan.h"
#include "deauther.h"

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
      char timeText[6];
      char secondText[3];
      snprintf(timeText, sizeof(timeText), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      snprintf(secondText, sizeof(secondText), "%02d", timeinfo.tm_sec);

      display.setTextSize(3);
      int16_t timeX = 0;
      int16_t timeY = 0;
      uint16_t timeW = 0;
      uint16_t timeH = 0;
      display.getTextBounds(timeText, 0, 0, &timeX, &timeY, &timeW, &timeH);

      display.setTextSize(1);
      int16_t secondX = 0;
      int16_t secondY = 0;
      uint16_t secondW = 0;
      uint16_t secondH = 0;
      display.getTextBounds(secondText, 0, 0, &secondX, &secondY, &secondW, &secondH);

      int totalWidth = timeW + 4 + secondW;
      int startX = (SCREEN_WIDTH - totalWidth) / 2;

      display.setTextSize(3);
      display.setCursor(startX - timeX, 14);
      display.print(timeText);

      display.setTextSize(1);
      display.setCursor(startX + timeW + 4 - secondX, 24);
      display.print(secondText);
    }
    else
    {
      display.setTextSize(2);
      display.setCursor(10, 24);
      display.println("Sem hora");
    }
  }
  else if (screen == 4)
  {
    display.setTextSize(1);

    const char *handle = "@ofc_edson_costa";
    int16_t textX = 0;
    int16_t textY = 0;
    uint16_t textW = 0;
    uint16_t textH = 0;
    display.getTextBounds(handle, 0, 0, &textX, &textY, &textW, &textH);

    int handleX = (SCREEN_WIDTH - textW) / 2;
    display.setCursor(handleX - textX, 30);
    display.print(handle);

    if (portalToastUntil > 0 && millis() < portalToastUntil)
    {
      display.fillRoundRect(18, 46, 92, 14, 4, BLACK);
      display.drawRoundRect(18, 46, 92, 14, 4, WHITE);
      display.setCursor(24, 49);
      display.print(portalToastMessage);
    }
  }
  else if (screen == 5)
  {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("Redes Wi-Fi"));

    if (g_wifiScanBusy && g_wifiScanNetworkCount == 0)
    {
      display.setCursor(0, 24);
      display.println(F("Escaneando..."));
    }
    else if (g_wifiScanNetworkCount == 0)
    {
      display.setCursor(0, 24);
      display.println(F("Nenhuma rede"));
    }
    else
    {
      constexpr int kMaxLines = 7;
      char line[22];

      for (int i = 0; i < g_wifiScanNetworkCount && i < kMaxLines; i++)
      {
        const char *name = g_wifiScanNetworks[i].ssid[0] ? g_wifiScanNetworks[i].ssid : "(oculto)";
        int prefix = snprintf(line, sizeof(line), "%2u %3d ", g_wifiScanNetworks[i].channel, static_cast<int>(g_wifiScanNetworks[i].rssi));
        if (prefix < 0)
        {
          prefix = 0;
        }
        if (static_cast<size_t>(prefix) >= sizeof(line))
        {
          prefix = static_cast<int>(sizeof(line)) - 1;
        }
        strncpy(line + prefix, name, sizeof(line) - 1 - static_cast<size_t>(prefix));
        line[sizeof(line) - 1] = '\0';

        display.setCursor(0, static_cast<int16_t>(8 + i * 8));
        display.print(line);
      }
    }
  }

  display.display();
}
