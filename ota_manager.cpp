#include "ota_manager.h"
#include "secrets.h"
#include "wifi_manager.h"
#include "file_manager.h"
#include <esp_ota_ops.h>

OTAManager otaManager;

bool OTAManager::downloadAndUpdateFirmware(const String& binUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, binUrl);
  http.addHeader("User-Agent", "ESP32");
  int binCode = http.GET();

  if (binCode == HTTP_CODE_MOVED_PERMANENTLY || binCode == HTTP_CODE_FOUND) {
    String redirectUrl = http.getLocation();
    Serial.printf("Redirect detected. Following to: %s\n", redirectUrl.c_str());
    http.end();
    http.begin(client, redirectUrl);
    http.addHeader("User-Agent", "ESP32");
    binCode = http.GET();
  }

  if (binCode != HTTP_CODE_OK) {
    Serial.printf("Failed to download binary. HTTP code: %d\n", binCode);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("Binary content empty.");
    http.end();
    return false;
  }

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    Serial.println("OTA partition error.");
    http.end();
    return false;
  }

  Serial.printf("Starting OTA. Size: %d bytes\n", contentLength);
  if (!Update.begin(contentLength, U_FLASH, update_partition->address)) {
    Serial.printf("OTA begin failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buf[1024];
  while (http.connected() && written < contentLength) {
    size_t len = stream->available();
    if (len) {
      if (len > sizeof(buf)) len = sizeof(buf);
      int c = stream->readBytes(buf, len);
      if (c > 0) {
        if (Update.write(buf, c) != c) {
          Serial.printf("OTA write error: %s\n", Update.errorString());
          http.end();
          return false;
        }
        written += c;
        Serial.printf("OTA progress: %d/%d bytes\n", written, contentLength);
      }
    }
  }

  bool success = Update.end(true);
  http.end();
  
  return success && Update.isFinished();
}

bool OTAManager::downloadHTMLFile() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, html_raw_url);
  http.addHeader("User-Agent", "ESP32");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Failed to download HTML. HTTP code: %d\n", httpCode);
    http.end();
    return false;
  }

  String htmlContent = http.getString();
  http.end();

  return fileManager.writeFile("/index.html", htmlContent);
}

void OTAManager::checkForUpdate() {
  if (!wifiManager.isConnected()) {
    Serial.println("WiFi not connected. Aborting OTA.");
    return;
  }

  Serial.println("Checking GitHub API for update...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, github_api);
  http.addHeader("User-Agent", "ESP32");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("GitHub API error: %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return;
  }

  String latestVersion = doc["tag_name"];
  if (latestVersion == firmware_version) {
    Serial.println("Firmware is already up to date.");
    return;
  }

  JsonArray assets = doc["assets"];
  if (assets.size() == 0) {
    Serial.println("No binary in release.");
    return;
  }

  String binUrl = assets[0]["browser_download_url"];
  Serial.printf("New release %s found. Downloading from %s\n", latestVersion.c_str(), binUrl.c_str());

  if (downloadAndUpdateFirmware(binUrl)) {
    Serial.println("OTA update completed successfully!");
    if (downloadHTMLFile()) {
      Serial.println("HTML file updated successfully!");
    } else {
      Serial.println("Failed to update HTML file, but firmware update was successful");
    }
    Serial.println("Rebooting...");
    ESP.restart();
  } else {
    Serial.println("OTA update failed.");
  }
}

bool OTAManager::updateHTMLFromGitHub() {
  if (!wifiManager.isConnected()) {
    Serial.println("WiFi not connected. Cannot update HTML.");
    return false;
  }
  return downloadHTMLFile();
}