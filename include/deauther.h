#pragma once

#include <Arduino.h>

void initDeauther();
void startDeauthAttack(uint8_t *apMac, uint8_t *clientMac, uint8_t channel, uint8_t reason = 7);
void stopDeauthAttack();
bool isDeauthActive();
void updateDeauth();
void toggleDeauther();