#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <time.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== WIFI =====
const char *ssid = "OLIVEIRA";
const char *password = "residencial242";

// ===== dados =====
float temp = 0;

int screen = 0;
unsigned long lastScreenChange = 0;
unsigned long lastWeatherUpdate = 0;

// ===== Eye Animation (estilo Intellar) =====
const int eyeWidth = 38;
const int eyeHeight = 24;
const int eyeRadius = 8;
const int eyeGap = 10;
const int pupilRadius = 4;

int pupilOffsetX = 0;
int pupilOffsetY = 0;
int eyelid = 0;
bool closingBlink = false;
bool angryEyes = false;
bool deadEyes = false;

unsigned long lastEyeFrame = 0;
unsigned long nextBlinkAt = 0;
unsigned long nextLookAt = 0;
unsigned long angryUntil = 0;

const unsigned long angryHoldMs = 5000;

void updateEyeAnimation()
{
  unsigned long now = millis();

  if (now - lastEyeFrame < 40)
  {
    return;
  }

  lastEyeFrame = now;

  if (nextBlinkAt == 0)
  {
    nextBlinkAt = now + random(1500, 3500);
    nextLookAt = now + random(600, 1200);
  }

  if (now >= nextBlinkAt || eyelid > 0)
  {
    if (eyelid == 0)
    {
      closingBlink = true;
    }

    if (closingBlink)
    {
      eyelid += 4;
      if (eyelid >= eyeHeight)
      {
        eyelid = eyeHeight;
        closingBlink = false;
      }
    }
    else
    {
      eyelid -= 4;
      if (eyelid <= 0)
      {
        eyelid = 0;
        nextBlinkAt = now + random(2000, 4500);
      }
    }
  }

  if (now >= nextLookAt && eyelid < eyeHeight - 6)
  {
    int look = random(0, 5);

    if (look == 0)
    {
      pupilOffsetX = 0;
      pupilOffsetY = 0;
    }
    else if (look == 1)
    {
      pupilOffsetX = -6;
      pupilOffsetY = 0;
    }
    else if (look == 2)
    {
      pupilOffsetX = 6;
      pupilOffsetY = 0;
    }
    else if (look == 3)
    {
      pupilOffsetX = 0;
      pupilOffsetY = -3;
    }
    else
    {
      pupilOffsetX = 0;
      pupilOffsetY = 3;
    }

    nextLookAt = now + random(600, 1600);
  }
}

void drawAngryBrows(int leftX, int rightX, int y)
{
  display.drawLine(leftX + 6, y + 6, leftX + eyeWidth - 6, y + 1, BLACK);
  display.drawLine(leftX + 6, y + 7, leftX + eyeWidth - 6, y + 2, BLACK);

  display.drawLine(rightX + 6, y + 1, rightX + eyeWidth - 6, y + 6, BLACK);
  display.drawLine(rightX + 6, y + 2, rightX + eyeWidth - 6, y + 7, BLACK);
}

void drawDeadEyes(int leftX, int rightX, int y)
{
  int eyeCenterY = y + eyeHeight / 2;

  display.drawLine(leftX + 8, y + 6, leftX + eyeWidth - 8, y + eyeHeight - 6, BLACK);
  display.drawLine(leftX + 8, y + eyeHeight - 6, leftX + eyeWidth - 8, y + 6, BLACK);

  display.drawLine(rightX + 8, y + 6, rightX + eyeWidth - 8, y + eyeHeight - 6, BLACK);
  display.drawLine(rightX + 8, y + eyeHeight - 6, rightX + eyeWidth - 8, y + 6, BLACK);

  display.drawLine(leftX + 12, eyeCenterY + 9, leftX + eyeWidth - 12, eyeCenterY + 9, BLACK);
  display.drawLine(rightX + 12, eyeCenterY + 9, rightX + eyeWidth - 12, eyeCenterY + 9, BLACK);
}

void drawEyeAnimation()
{
  deadEyes = WiFi.status() != WL_CONNECTED;
  angryEyes = !deadEyes && millis() < angryUntil;

  int totalEyesWidth = (eyeWidth * 2) + eyeGap;
  int startX = (SCREEN_WIDTH - totalEyesWidth) / 2;
  int y = 18;

  int leftX = startX;
  int rightX = startX + eyeWidth + eyeGap;

  display.fillRoundRect(leftX, y, eyeWidth, eyeHeight, eyeRadius, WHITE);
  display.fillRoundRect(rightX, y, eyeWidth, eyeHeight, eyeRadius, WHITE);

  int pupilMinX = -(eyeWidth / 2 - pupilRadius - 4);
  int pupilMaxX = (eyeWidth / 2 - pupilRadius - 4);
  int pupilMinY = -(eyeHeight / 2 - pupilRadius - 4);
  int pupilMaxY = (eyeHeight / 2 - pupilRadius - 4);

  int px = constrain(pupilOffsetX, pupilMinX, pupilMaxX);
  int py = constrain(pupilOffsetY, pupilMinY, pupilMaxY);

  int leftCenterX = leftX + eyeWidth / 2;
  int rightCenterX = rightX + eyeWidth / 2;
  int centerY = y + eyeHeight / 2;

  if (deadEyes)
  {
    drawDeadEyes(leftX, rightX, y);
    return;
  }

  display.fillCircle(leftCenterX + px, centerY + py, pupilRadius, BLACK);
  display.fillCircle(rightCenterX + px, centerY + py, pupilRadius, BLACK);

  if (angryEyes)
  {
    drawAngryBrows(leftX, rightX, y);
    display.fillRect(leftX + 4, y + 2, eyeWidth - 8, 3, BLACK);
    display.fillRect(rightX + 4, y + 2, eyeWidth - 8, 3, BLACK);
  }

  if (eyelid > 0)
  {
    int topCover = eyelid / 2;
    int bottomCover = eyelid - topCover;

    display.fillRect(leftX, y, eyeWidth, topCover, BLACK);
    display.fillRect(rightX, y, eyeWidth, topCover, BLACK);

    display.fillRect(leftX, y + eyeHeight - bottomCover, eyeWidth, bottomCover, BLACK);
    display.fillRect(rightX, y + eyeHeight - bottomCover, eyeWidth, bottomCover, BLACK);
  }
}

// ===== clima =====
void getWeather()
{
  WiFiClient client;
  HTTPClient http;
  unsigned long startedAt = millis();

  String url = "http://api.open-meteo.com/v1/forecast?latitude=-1.29&longitude=-47.93&current_weather=true";

  http.begin(client, url);
  int code = http.GET();
  unsigned long latency = millis() - startedAt;

  if (latency >= 1800)
  {
    angryUntil = millis() + angryHoldMs;
  }

  if (code > 0)
  {
    String payload = http.getString();

    StaticJsonDocument<1024> doc;
    deserializeJson(doc, payload);

    temp = doc["current_weather"]["temperature"];
  }

  http.end();
}

// ===== WIFI =====
void connectWiFi()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Conectando WiFi...");
  display.display();

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(300);
  }
}

// ===== SETUP =====
void setup()
{
  Serial.begin(115200);

  Wire.begin(14, 12);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    while (true)
      ;
  }

  connectWiFi();

  configTime(-3 * 3600, 0, "pool.ntp.org");

  randomSeed(micros());

  getWeather();
}

// ===== TELAS =====
void drawScreen()
{
  display.clearDisplay();
  display.setTextColor(WHITE);

  // ===== TELA 0 - EYES =====
  if (screen == 0)
  {
    updateEyeAnimation();
    drawEyeAnimation();
  }

  // ===== TELA 1 - WIFI =====
  else if (screen == 1)
  {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("WIFI STATUS");

    display.setCursor(0, 20);
    display.print("Status: ");
    display.println(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");

    display.setCursor(0, 35);
    display.print("IP:");
    display.println(WiFi.localIP());

    display.setCursor(0, 50);
    display.print("RSSI:");
    display.print(WiFi.RSSI());
    display.println(" dBm");
  }

  // ===== TELA 2 - TEMPERATURA =====
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

  // ===== TELA 3 - RELOGIO =====
  else if (screen == 3)
  {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("RELOGIO");

    struct tm timeinfo;

    if (getLocalTime(&timeinfo))
    {
      display.setTextSize(2);
      display.setCursor(10, 25);
      display.printf("%02d:%02d:%02d",
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
    }
    else
    {
      display.setCursor(0, 30);
      display.println("Sem hora");
    }
  }

  display.display();
}

// ===== LOOP =====
void loop()
{
  // atualiza clima
  if (millis() - lastWeatherUpdate > 60000)
  {
    getWeather();
    lastWeatherUpdate = millis();
  }

  // troca tela
  if (millis() - lastScreenChange > 8000)
  {
    screen++;
    if (screen > 3)
      screen = 0;

    lastScreenChange = millis();
  }

  drawScreen();
}
