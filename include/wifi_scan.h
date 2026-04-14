#pragma once

#include <cstdint>

constexpr int kWifiScanMaxNetworks = 16;

struct WifiScanNetwork
{
  char ssid[33];
  int32_t rssi;
  uint8_t channel;
  uint8_t bssid[6];
};

extern WifiScanNetwork g_wifiScanNetworks[kWifiScanMaxNetworks];
extern int g_wifiScanNetworkCount;
extern bool g_wifiScanBusy;

void wifiScanTick();

/** Grava em g_wifiScanNetworks o resultado de um WiFi.scanNetworks ja concluido (ex.: /api/scan). */
void wifiScanStoreDriverResults(int rawCount);
