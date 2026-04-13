#include "weather.h"

#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

#include "app_state.h"

void getWeather()
{
  WiFiClient client;
  HTTPClient http;
  unsigned long startedAt = millis();

  String url = "http://api.open-meteo.com/v1/forecast?latitude=-1.29&longitude=-47.93&current_weather=true";

  http.begin(client, url);
  int code = http.GET();
  unsigned long latency = millis() - startedAt;

  if (code <= 0 || latency >= 2800)
  {
    evilUntil = millis() + evilHoldMs;
  }
  else if (latency >= 1800)
  {
    angryUntil = millis() + angryHoldMs;
  }

  if (code > 0)
  {
    String payload = http.getString();

    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok)
    {
      temp = doc["current_weather"]["temperature"] | temp;
    }
  }

  http.end();
}
