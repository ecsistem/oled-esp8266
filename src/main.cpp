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

// ===== fantasma =====
void drawGhost(int x, int y)
{
  display.drawCircle(x, y, 10, WHITE);
  display.fillCircle(x - 3, y - 2, 1, WHITE);
  display.fillCircle(x + 3, y - 2, 1, WHITE);
  display.drawLine(x - 5, y + 5, x + 5, y + 5, WHITE);
}

// ===== clima =====
void getWeather()
{
  WiFiClient client;
  HTTPClient http;

  String url = "http://api.open-meteo.com/v1/forecast?latitude=-1.29&longitude=-47.93&current_weather=true";

  http.begin(client, url);
  int code = http.GET();

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

  getWeather();
}

// ===== TELAS =====
void drawScreen()
{
  display.clearDisplay();
  display.setTextColor(WHITE);

  // ===== TELA 0 - FANTASMA =====
  if (screen == 0)
  {
    drawGhost(64, 28);

    display.setTextSize(1);
    display.setCursor(30, 55);
    // display.println("Dev Mode 👻");
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
  if (millis() - lastScreenChange > 4000)
  {
    screen++;
    if (screen > 3)
      screen = 0;

    lastScreenChange = millis();
  }

  drawScreen();
}
