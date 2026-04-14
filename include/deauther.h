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
void startProbeAttack();
void stopProbeAttack();
void updateProbe();

/** Volta politica de pais WiFi ao padrao (apos injecao). */
void restoreWifiRegAfterInjection();

extern String deautherApMac;
extern String deautherClientMac;
extern int deautherChannel;
extern bool deautherRunning;
/** Ultimo segundo completo: copia de deauth.packetCounter (Attack::updateCounter). */
extern unsigned long deautherPacketsSent;
/** Ultimo segundo completo: copia de tmpPacketRate -> packetRate (Attack::updateCounter). */
extern unsigned long deautherTmpPacketRate;
/** Chamadas em que o driver recusou o envio (retorno != 0) na camada sendPacket do deauth. */
extern unsigned long deautherInjectFail;
extern bool beaconActive;
extern unsigned long beaconPacketsSent;
extern bool probeActive;
extern unsigned long probePacketsSent;
/** Ultimo segundo completo: beacon.packetCounter (paridade v2). */
extern unsigned long beaconPacketsPerSecond;
/** Ultimo segundo completo: probe.packetCounter (paridade v2). */
extern unsigned long probePacketsPerSecond;
/** maxPkts calculado para deauth no estado atual. */
extern unsigned long deauthMaxPacketsPerSecond;
/** maxPkts calculado para beacon no estado atual. */
extern unsigned long beaconMaxPacketsPerSecond;
/** maxPkts calculado para probe no estado atual. */
extern unsigned long probeMaxPacketsPerSecond;
