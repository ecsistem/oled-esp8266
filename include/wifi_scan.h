#pragma once

#include <cstdint>

constexpr int kWifiScanMaxNetworks = 16;

struct WifiScanNetwork
{
  char ssid[33];
  int32_t rssi;
  uint8_t channel;
};

extern WifiScanNetwork g_wifiScanNetworks[kWifiScanMaxNetworks];
extern int g_wifiScanNetworkCount;
extern bool g_wifiScanBusy;

void wifiScanTick();
