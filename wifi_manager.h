#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

enum WifiConnState_t { WIFI_OK, WIFI_DISCONNECTED, WIFI_RECONNECTING, WIFI_WAIT };

class WiFiManager {
private:
  const unsigned long reconnectDelay = 5000UL;
  const int maxReconnectAttempts = 5;
  const int maxRebootsBeforeWait = 3;
  const unsigned long waitAfterFails = 1800000UL;

  int rebootFailCount = 0;
  unsigned long lastReconnectAttempt = 0;
  int reconnectAttempts = 0;
  unsigned long waitStart = 0;

public:
  WifiConnState_t wifiState = WIFI_OK;
  
  void connect();
  void handleWiFi();
  bool isConnected();
};

extern WiFiManager wifiManager;

#endif