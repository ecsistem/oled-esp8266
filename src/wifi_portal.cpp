#include "wifi_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include "app_state.h"

namespace
{
  constexpr byte DNS_PORT = 53;
  DNSServer dnsServer;

  String escapeHtml(const String &value)
  {
    String escaped;

    for (size_t i = 0; i < value.length(); i++)
    {
      char c = value[i];

      if (c == '&')
        escaped += "&amp;";
      else if (c == '<')
        escaped += "&lt;";
      else if (c == '>')
        escaped += "&gt;";
      else if (c == '"')
        escaped += "&quot;";
      else
        escaped += c;
    }

    return escaped;
  }

  void loadWiFiConfig()
  {
    if (!LittleFS.exists(wifiConfigPath))
    {
      return;
    }

    File file = LittleFS.open(wifiConfigPath, "r");
    if (!file)
    {
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
      return;
    }

    wifiSsid = doc["ssid"] | wifiSsid;
    wifiPassword = doc["password"] | wifiPassword;
    weatherUpdateIntervalMs = doc["weather_interval_ms"] | weatherUpdateIntervalMs;
    screenChangeIntervalMs = doc["screen_change_ms"] | screenChangeIntervalMs;
    timezoneOffsetHours = doc["timezone_offset_hours"] | timezoneOffsetHours;
    oledBrightness = doc["oled_brightness"] | oledBrightness;
  }

  bool saveWiFiConfig()
  {
    JsonDocument doc;
    doc["ssid"] = wifiSsid;
    doc["password"] = wifiPassword;
    doc["weather_interval_ms"] = weatherUpdateIntervalMs;
    doc["screen_change_ms"] = screenChangeIntervalMs;
    doc["timezone_offset_hours"] = timezoneOffsetHours;
    doc["oled_brightness"] = oledBrightness;

    File file = LittleFS.open(wifiConfigPath, "w");
    if (!file)
    {
      return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
  }

  unsigned long parseULongBounded(const String &raw, unsigned long minValue, unsigned long maxValue, unsigned long fallback)
  {
    if (raw.length() == 0)
    {
      return fallback;
    }

    unsigned long value = strtoul(raw.c_str(), nullptr, 10);
    if (value < minValue)
      return minValue;
    if (value > maxValue)
      return maxValue;
    return value;
  }

  int parseIntBounded(const String &raw, int minValue, int maxValue, int fallback)
  {
    if (raw.length() == 0)
    {
      return fallback;
    }

    long value = strtol(raw.c_str(), nullptr, 10);
    if (value < minValue)
      return minValue;
    if (value > maxValue)
      return maxValue;
    return static_cast<int>(value);
  }

  void applyDisplayAndTimeSettings()
  {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(oledBrightness);
    configTime(timezoneOffsetHours * 3600, 0, "pool.ntp.org");
  }

  void connectStationWiFi()
  {
    lastConnectAttemptAt = millis();
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

    unsigned long startedAt = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000)
    {
      delay(300);
    }
  }

  String wifiStatusToText(wl_status_t status)
  {
    if (status == WL_CONNECTED)
      return "Conectado";
    if (status == WL_NO_SSID_AVAIL)
      return "SSID nao encontrado";
    if (status == WL_CONNECT_FAILED)
      return "Falha de autenticacao";
    if (status == WL_CONNECTION_LOST)
      return "Conexao perdida";
    if (status == WL_IDLE_STATUS)
      return "Conectando";
    if (status == WL_DISCONNECTED)
      return "Desconectado";
    return "Status desconhecido";
  }

  void startHotspot()
  {
    apSsid = "OLED-" + String(ESP.getChipId(), HEX);

    WiFi.softAPdisconnect(true);
    delay(100);

    bool apStarted = WiFi.softAP(apSsid.c_str(), apPassword, 1, false, 4);

    if (!apStarted)
    {
      WiFi.softAP(apSsid.c_str());
    }

    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  }

  void handleCaptiveRedirect()
  {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  }

  String buildWifiSelectOptions()
  {
    String options;
    int networkCount = WiFi.scanNetworks();

    if (networkCount <= 0)
    {
      options += "<option value=''>Nenhuma rede encontrada</option>";
      return options;
    }

    for (int i = 0; i < networkCount; i++)
    {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0)
      {
        continue;
      }

      int rssi = WiFi.RSSI(i);
      String selected = ssid == wifiSsid ? " selected" : "";
      options += "<option value='" + escapeHtml(ssid) + "'" + selected + ">";
      options += escapeHtml(ssid) + " (" + String(rssi) + " dBm)";
      options += "</option>";
    }

    WiFi.scanDelete();
    return options;
  }

  void handleRoot()
  {
    String wifiOptions = buildWifiSelectOptions();
    wl_status_t staStatus = WiFi.status();
    bool staConnected = staStatus == WL_CONNECTED;

    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<style>body{font-family:Arial,sans-serif;background:#111;color:#eee;max-width:560px;margin:0 auto;padding:20px}input,select,button{width:100%;padding:12px;margin:8px 0;border-radius:10px;border:0}input,select{background:#222;color:#fff}button{background:#00a6ff;color:#fff;font-weight:bold} .card{background:#1b1b1b;padding:18px;border-radius:16px;margin-bottom:12px} .muted{color:#aaa;font-size:14px} .secondary{background:#2a2a2a} .ok{color:#58d68d}.bad{color:#ff7675}</style></head><body>";
    page += "<div class='card'><h2>Configurar Wi-Fi</h2>";
    page += "<p class='muted'>Conectado ao hotspot: " + apSsid + "</p>";
    page += "<p class='muted'>Status STA: ";
    page += staConnected ? "<span class='ok'>Conectado</span>" : "<span class='bad'>" + wifiStatusToText(staStatus) + "</span>";
    page += "</p>";
    page += "<p class='muted'>IP AP: " + WiFi.softAPIP().toString() + "</p>";
    page += "<p class='muted'>IP STA: ";
    page += staConnected ? WiFi.localIP().toString() : "-";
    page += "</p>";
    page += "<form method='post' action='/save'>";
    page += "<select name='ssid' required>";
    page += wifiOptions;
    page += "</select>";
    page += "<input name='password' placeholder='Senha do Wi-Fi' type='password' value='" + escapeHtml(wifiPassword) + "'>";
    page += "<h3>Ajustes do Sistema</h3>";
    page += "<label>Intervalo do clima (segundos)</label>";
    page += "<input name='weather_sec' type='number' min='10' max='3600' value='" + String(weatherUpdateIntervalMs / 1000) + "'>";
    page += "<label>Fuso horario (UTC, ex: -3)</label>";
    page += "<input name='tz' type='number' min='-12' max='14' value='" + String(timezoneOffsetHours) + "'>";
    page += "<label>Brilho OLED (0-255)</label>";
    page += "<input name='brightness' type='number' min='0' max='255' value='" + String(oledBrightness) + "'>";
    page += "<label>Troca de tela (segundos)</label>";
    page += "<input name='screen_sec' type='number' min='2' max='120' value='" + String(screenChangeIntervalMs / 1000) + "'>";
    page += "<button type='submit'>Salvar e conectar</button></form>";
    page += "<form method='get' action='/'><button class='secondary' type='submit'>Atualizar lista de redes</button></form>";
    page += "<form method='get' action='/status'><button class='secondary' type='submit'>Ver status completo</button></form>";
    page += "<p class='muted'>API: /api/status</p>";
    page += "</div></body></html>";

    server.send(200, "text/html; charset=utf-8", page);
  }

  void handleStatusPage()
  {
    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<style>body{font-family:Arial,sans-serif;background:#0e0f12;color:#f4f4f4;max-width:620px;margin:0 auto;padding:20px} .card{background:#1a1d23;padding:16px;border-radius:14px;margin:10px 0} .label{color:#9aa4b2;font-size:13px} .value{font-size:18px} .ok{color:#53d769} .bad{color:#ff6b6b} a{color:#7cc7ff} button{width:100%;padding:12px;margin-top:10px;border:0;border-radius:10px;background:#2a2f3a;color:#fff}</style>";
    page += "<script>async function r(){try{const s=await fetch('/api/status');const j=await s.json();document.getElementById('sta').innerText=j.sta_status;document.getElementById('staIp').innerText=j.sta_ip;document.getElementById('ap').innerText=j.ap_ssid;document.getElementById('apIp').innerText=j.ap_ip;document.getElementById('clients').innerText=j.ap_clients;document.getElementById('ssid').innerText=j.saved_ssid;document.getElementById('rssi').innerText=j.rssi;document.getElementById('last').innerText=j.last_connect_attempt_age_ms+' ms';document.getElementById('uptime').innerText=j.uptime_ms+' ms';document.getElementById('temp').innerText=j.temperature_c+' C';document.getElementById('screen').innerText=j.active_screen;}catch(e){}}setInterval(r,2000);window.onload=r;</script></head><body>";
    page += "<h2>Status do Servidor Wi-Fi</h2>";
    page += "<div class='card'><div class='label'>STA Status</div><div class='value' id='sta'>-</div><div class='label'>STA IP</div><div class='value' id='staIp'>-</div><div class='label'>RSSI</div><div class='value' id='rssi'>-</div></div>";
    page += "<div class='card'><div class='label'>AP SSID</div><div class='value' id='ap'>-</div><div class='label'>AP IP</div><div class='value' id='apIp'>-</div><div class='label'>Clientes no AP</div><div class='value' id='clients'>-</div></div>";
    page += "<div class='card'><div class='label'>SSID salvo</div><div class='value' id='ssid'>-</div><div class='label'>Ultima tentativa de conexao</div><div class='value' id='last'>-</div><div class='label'>Uptime</div><div class='value' id='uptime'>-</div><div class='label'>Temperatura</div><div class='value' id='temp'>-</div><div class='label'>Tela ativa</div><div class='value' id='screen'>-</div></div>";
    page += "<button onclick=\"location.href='/'\">Voltar para configuracao</button>";
    page += "<div class='card'><a href='/api/status'>Abrir JSON de status</a></div></body></html>";

    server.send(200, "text/html; charset=utf-8", page);
  }

  void handleStatusJson()
  {
    JsonDocument doc;
    wl_status_t staStatus = WiFi.status();
    bool staConnected = staStatus == WL_CONNECTED;

    doc["sta_status"] = wifiStatusToText(staStatus);
    doc["sta_connected"] = staConnected;
    doc["sta_ip"] = staConnected ? WiFi.localIP().toString() : String("-");
    doc["rssi"] = staConnected ? String(WiFi.RSSI()) + " dBm" : String("-");
    doc["ap_ssid"] = apSsid;
    doc["ap_ip"] = WiFi.softAPIP().toString();
    doc["ap_clients"] = WiFi.softAPgetStationNum();
    doc["saved_ssid"] = wifiSsid;
    doc["last_connect_attempt_ms"] = lastConnectAttemptAt;
    doc["last_connect_attempt_age_ms"] = lastConnectAttemptAt > 0 ? millis() - lastConnectAttemptAt : 0;
    doc["uptime_ms"] = millis();
    doc["temperature_c"] = temp;
    doc["active_screen"] = screen;
    doc["weather_update_interval_ms"] = weatherUpdateIntervalMs;
    doc["screen_change_interval_ms"] = screenChangeIntervalMs;
    doc["timezone_offset_hours"] = timezoneOffsetHours;
    doc["oled_brightness"] = oledBrightness;

    String payload;
    serializeJson(doc, payload);
    server.send(200, "application/json; charset=utf-8", payload);
  }

  void handleSave()
  {
    String newSsid = server.arg("ssid");
    String newPassword = server.arg("password");
    unsigned long weatherSec = parseULongBounded(server.arg("weather_sec"), 10, 3600, weatherUpdateIntervalMs / 1000);
    unsigned long screenSec = parseULongBounded(server.arg("screen_sec"), 2, 120, screenChangeIntervalMs / 1000);
    int newTz = parseIntBounded(server.arg("tz"), -12, 14, timezoneOffsetHours);
    int newBrightness = parseIntBounded(server.arg("brightness"), 0, 255, oledBrightness);

    if (newSsid.length() == 0)
    {
      server.send(400, "text/plain; charset=utf-8", "SSID vazio");
      return;
    }

    wifiSsid = newSsid;
    wifiPassword = newPassword;
    weatherUpdateIntervalMs = weatherSec * 1000;
    screenChangeIntervalMs = screenSec * 1000;
    timezoneOffsetHours = newTz;
    oledBrightness = static_cast<uint8_t>(newBrightness);

    applyDisplayAndTimeSettings();
    bool saved = saveWiFiConfig();

    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<meta http-equiv='refresh' content='4;url=/status'>";
    page += "<style>body{font-family:Arial,sans-serif;background:#101216;color:#f4f4f4;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}.card{background:#1b1f27;padding:22px;border-radius:16px;max-width:460px;width:100%}.ok{color:#53d769}.bad{color:#ff6b6b}.loader{width:18px;height:18px;border-radius:50%;border:3px solid #4f5d75;border-top-color:#fff;display:inline-block;animation:s 1s linear infinite;vertical-align:middle;margin-right:8px}@keyframes s{to{transform:rotate(360deg)}}</style></head><body>";
    page += "<div class='card'><h3>Salvando configuracao</h3>";
    page += "<p>SSID selecionado: <strong>" + escapeHtml(wifiSsid) + "</strong></p>";
    page += saved ? "<p class='ok'>Dados salvos com sucesso.</p>" : "<p class='bad'>Falha ao salvar no armazenamento.</p>";
    page += "<p><span class='loader'></span>Tentando conectar agora...</p>";
    page += "<p>Voce sera redirecionado para a pagina de status em alguns segundos.</p>";
    page += "<p><a href='/status' style='color:#7cc7ff'>Abrir status agora</a></p></div></body></html>";

    server.send(200, "text/html; charset=utf-8", page);

    WiFi.disconnect();
    connectStationWiFi();
  }

  void startConfigPortal()
  {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
    server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
    server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);
    server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
    server.on("/fwlink", HTTP_GET, handleCaptiveRedirect);
    server.on("/status", HTTP_GET, handleStatusPage);
    server.on("/api/status", HTTP_GET, handleStatusJson);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleCaptiveRedirect);
    server.begin();
  }
} // namespace

void initWiFiAndPortal()
{
  LittleFS.begin();
  loadWiFiConfig();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  startHotspot();
  startConfigPortal();
  applyDisplayAndTimeSettings();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Conectando WiFi...");
  display.display();

  connectStationWiFi();
}

void handlePortalClient()
{
  server.handleClient();
  dnsServer.processNextRequest();
}
