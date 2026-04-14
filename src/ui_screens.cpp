#include "ui_screens.h"

#include <time.h>
#include <ESP8266WiFi.h>

#include "app_state.h"
#include "eye_animation.h"
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
    display.println(F("Wi-Fi Deauther"));

    display.setCursor(0, 16);
    if (deautherRunning)
    {
      display.println(F("ATAQUE ATIVO"));
      display.setCursor(0, 26);
      display.print(F("deauth:"));
      display.print(deautherPacketsSent);
      display.print(F(" tmp:"));
      display.println(deautherTmpPacketRate);
      display.setCursor(0, 38);
      display.print(F("Falha:"));
      display.println(deautherInjectFail);
      display.setCursor(0, 52);
      display.println(F("FLASH=parar"));
    }
    else
    {
      display.println(F("ATAQUE PARADO"));
      display.setCursor(0, 32);
      display.println(F("Configure via web"));
      display.setCursor(0, 48);
      display.println(F("FLASH para INICIAR"));
    }
  }

  display.display();
}
