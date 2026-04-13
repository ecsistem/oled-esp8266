#pragma once

void initWiFiAndPortal();
void handlePortalClient();
void setCaptivePortalEnabled(bool enabled);
void toggleCaptivePortalEnabled();

/** Restaura WIFI_AP_STA, reabre o SoftAP e reconecta o STA apos deauther/beacon. */
void restorePortalWiFiAfterDeauth();
