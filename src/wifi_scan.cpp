#include "wifi_scan.h"

#include <cstring>

#include <ESP8266WiFi.h>

#include "app_state.h"

WifiScanNetwork g_wifiScanNetworks[kWifiScanMaxNetworks];
int g_wifiScanNetworkCount = 0;
bool g_wifiScanBusy = false;

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

  void fillGlobalsFromDriverIndices(int rawCount)
  {
    if (rawCount < 0)
    {
      g_wifiScanNetworkCount = 0;
      return;
    }
    g_wifiScanNetworkCount = rawCount > kWifiScanMaxNetworks ? kWifiScanMaxNetworks : rawCount;

    for (int i = 0; i < g_wifiScanNetworkCount; i++)
    {
      String s = WiFi.SSID(i);
      strncpy(g_wifiScanNetworks[i].ssid, s.c_str(), sizeof(g_wifiScanNetworks[i].ssid) - 1);
      g_wifiScanNetworks[i].ssid[sizeof(g_wifiScanNetworks[i].ssid) - 1] = '\0';
      g_wifiScanNetworks[i].rssi = WiFi.RSSI(i);
      g_wifiScanNetworks[i].channel = static_cast<uint8_t>(WiFi.channel(i));
      const uint8_t *bssid = WiFi.BSSID(i);
      if (bssid)
      {
        memcpy(g_wifiScanNetworks[i].bssid, bssid, 6);
      }
      else
      {
        memset(g_wifiScanNetworks[i].bssid, 0, 6);
      }
    }

    sortByRssiDescending(g_wifiScanNetworks, g_wifiScanNetworkCount);
  }

  void loadResultsFromDriver(int rawCount)
  {
    fillGlobalsFromDriverIndices(rawCount);
    WiFi.scanDelete();
    scanSessionActive = false;
    g_wifiScanBusy = false;
    lastScanCompleteMs = millis();
  }
}

void wifiScanStoreDriverResults(int rawCount)
{
  if (rawCount < 0)
  {
    g_wifiScanNetworkCount = 0;
    return;
  }
  g_wifiScanNetworkCount = rawCount > kWifiScanMaxNetworks ? kWifiScanMaxNetworks : rawCount;

  for (int i = 0; i < g_wifiScanNetworkCount; i++)
  {
    String s = WiFi.SSID(i);
    strncpy(g_wifiScanNetworks[i].ssid, s.c_str(), sizeof(g_wifiScanNetworks[i].ssid) - 1);
    g_wifiScanNetworks[i].ssid[sizeof(g_wifiScanNetworks[i].ssid) - 1] = '\0';
    g_wifiScanNetworks[i].rssi = WiFi.RSSI(i);
    g_wifiScanNetworks[i].channel = static_cast<uint8_t>(WiFi.channel(i));
    const uint8_t *bssid = WiFi.BSSID(i);
    if (bssid)
    {
      memcpy(g_wifiScanNetworks[i].bssid, bssid, 6);
    }
    else
    {
      memset(g_wifiScanNetworks[i].bssid, 0, 6);
    }
  }

  for (int i = 0; i < g_wifiScanNetworkCount; i++)
  {
    for (int j = i + 1; j < g_wifiScanNetworkCount; j++)
    {
      if (g_wifiScanNetworks[j].rssi > g_wifiScanNetworks[i].rssi)
      {
        WifiScanNetwork tmp = g_wifiScanNetworks[i];
        g_wifiScanNetworks[i] = g_wifiScanNetworks[j];
        g_wifiScanNetworks[j] = tmp;
      }
    }
  }
}

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
