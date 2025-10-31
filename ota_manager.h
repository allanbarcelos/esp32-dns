#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

class OTAManager {
private:
  const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";
  const char* html_raw_url = "https://raw.githubusercontent.com/allanbarcelos/esp32-dns/main/data/index.html";
  const unsigned long otaCheckInterval = 10 * 60 * 1000UL;

  unsigned long lastOtaCheck = 0;

  bool downloadAndUpdateFirmware(const String& binUrl);
  bool downloadHTMLFile();

public:
  void checkForUpdate();
  bool updateHTMLFromGitHub();
};

extern OTAManager otaManager;

#endif