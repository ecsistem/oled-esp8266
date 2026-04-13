#include "wifi_scan.h"

#include <cstring>

#include <ESP8266WiFi.h>

#include "app_state.h"

namespace
{
  constexpr unsigned long kRescanIntervalMs = 12000;

  bool scanSessionActive = false;
  unsigned long lastScanCompleteMs = 0;

  void sortByRssiDescending(WifiScanNetwork *nets, int n)
  {
    for (int i = 0; i < n; i++)
    {
      for (int j = i + 1; j < n; j++)
      {
        if (nets[j].rssi > nets[i].rssi)
        {
          WifiScanNetwork tmp = nets[i];
          nets[i] = nets[j];
          nets[j] = tmp;
        }
      }
    }
  }

  void loadResultsFromDriver(int rawCount)
  {
    g_wifiScanNetworkCount = rawCount > kWifiScanMaxNetworks ? kWifiScanMaxNetworks : rawCount;

    for (int i = 0; i < g_wifiScanNetworkCount; i++)
    {
      String s = WiFi.SSID(i);
      strncpy(g_wifiScanNetworks[i].ssid, s.c_str(), sizeof(g_wifiScanNetworks[i].ssid) - 1);
      g_wifiScanNetworks[i].ssid[sizeof(g_wifiScanNetworks[i].ssid) - 1] = '\0';
      g_wifiScanNetworks[i].rssi = WiFi.RSSI(i);
      g_wifiScanNetworks[i].channel = static_cast<uint8_t>(WiFi.channel(i));
    }

    sortByRssiDescending(g_wifiScanNetworks, g_wifiScanNetworkCount);
    WiFi.scanDelete();
    scanSessionActive = false;
    g_wifiScanBusy = false;
    lastScanCompleteMs = millis();
  }
}

WifiScanNetwork g_wifiScanNetworks[kWifiScanMaxNetworks];
int g_wifiScanNetworkCount = 0;
bool g_wifiScanBusy = false;

void wifiScanTick()
{
  const int scanState = WiFi.scanComplete();

  if (scanState == WIFI_SCAN_FAILED)
  {
    scanSessionActive = false;
    g_wifiScanBusy = false;
    WiFi.scanDelete();
    return;
  }

  if (scanState == WIFI_SCAN_RUNNING)
  {
    g_wifiScanBusy = scanSessionActive;
    return;
  }

  if (scanSessionActive && scanState >= 0)
  {
    loadResultsFromDriver(scanState);
  }
  else
  {
    g_wifiScanBusy = false;
  }

  const bool screenWantsScan = screen == 5 && screen5Enabled;
  if (!screenWantsScan)
  {
    return;
  }

  if (!scanSessionActive &&
      (lastScanCompleteMs == 0 || millis() - lastScanCompleteMs >= kRescanIntervalMs))
  {
    WiFi.scanNetworks(true, true);
    scanSessionActive = true;
    g_wifiScanBusy = true;
  }
}
