#pragma once

#include <Arduino.h>

void initDeauther();
void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason = 7);
void stopDeauthAttack();
bool isDeauthActive();
void updateDeauth();
void toggleDeauther();
void startBeaconAttack();
void stopBeaconAttack();
void updateBeacon();

/** Volta politica de pais WiFi ao padrao (apos injecao). */
void restoreWifiRegAfterInjection();

extern String deautherApMac;
extern String deautherClientMac;
extern int deautherChannel;
extern bool deautherRunning;
/** Pacotes aceites pelo driver (retorno 0 de wifi_send_pkt_freedom). */
extern unsigned long deautherPacketsSent;
/** Chamadas em que o driver recusou o envio (retorno != 0). */
extern unsigned long deautherInjectFail;
extern bool beaconActive;
extern unsigned long beaconPacketsSent;
