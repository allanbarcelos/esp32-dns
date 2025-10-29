#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "secrets.h"

// OTA settings
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";
const unsigned long checkInterval = 10 * 60 * 1000; // 10 minutes
unsigned long lastCheck = 0;

// Web server
WebServer server(80);

void setup() {
  // Reliable Serial since boot
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(500);

  Serial.println("=== Initializing ESP32 OTA with rollback ===");

  // Check if current firmware is new and mark as valid
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_NEW) {
      Serial.println("New firmware detected. Confirming image...");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());

  // Web routes
  server.on("/", handleRoot);
  server.begin();

  // Initial update check
  checkForUpdate();
  lastCheck = millis();
}

void loop() {
  server.handleClient();

  if (millis() - lastCheck > checkInterval) {
    checkForUpdate();
    lastCheck = millis();
  }

  delay(200);
  Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());

}

void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Aborting OTA.");
    return;
  }

  Serial.println("Checking GitHub API for update...");

  WiFiClientSecure client;
  client.setInsecure(); // Ignore SSL
  HTTPClient http;

  http.begin(client, github_api);
  http.addHeader("User-Agent", "ESP32");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Failed to access GitHub API. Code: %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  // Parse JSON
  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("Error parsing JSON: %s\n", error.c_str());
    return;
  }

  String latestVersion = doc["tag_name"];
  if (latestVersion == firmware_version) {
    Serial.println("Firmware is already up to date.");
    return;
  }

  JsonArray assets = doc["assets"];
  if (assets.size() == 0) {
    Serial.println("No binary found in release.");
    return;
  }

  String binUrl = assets[0]["browser_download_url"];
  Serial.printf("New release found: %s\n", latestVersion.c_str());
  Serial.printf("Downloading binary from: %s\n", binUrl.c_str());

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    Serial.println("Error getting OTA target partition.");
    return;
  }

  HTTPClient binHttp;
  binHttp.begin(client, binUrl);
  binHttp.addHeader("User-Agent", "ESP32");

  int binCode = binHttp.GET();

  // If 302 (redirect), follow automatically
  if (binCode == HTTP_CODE_MOVED_PERMANENTLY || binCode == HTTP_CODE_FOUND) {
    String redirectUrl = binHttp.getLocation();
    Serial.printf("Redirect detected. Following to: %s\n", redirectUrl.c_str());
    binHttp.end();
    binHttp.begin(client, redirectUrl);
    binHttp.addHeader("User-Agent", "ESP32");
    binCode = binHttp.GET();
  }

  if (binCode != HTTP_CODE_OK) {
    Serial.printf("Failed to download binary. Code: %d\n", binCode);
    binHttp.end();
    return;
  }

  int contentLength = binHttp.getSize();
  WiFiClient* stream = binHttp.getStreamPtr();

  if (contentLength <= 0) {
    Serial.println("Binary content empty. Aborting OTA.");
    binHttp.end();
    return;
  }

  Serial.printf("Starting OTA. Binary size: %d bytes\n", contentLength);
  if (!Update.begin(contentLength, U_FLASH, update_partition->address)) {
    Serial.printf("Failed to start OTA: %s\n", Update.errorString());
    binHttp.end();
    return;
  }

  size_t written = Update.writeStream(*stream);
  if (Update.end(true)) {
    if (Update.isFinished()) {
      Serial.println("OTA update completed successfully!");
    } else {
      Serial.println("Update did not finish correctly.");
    }
  } else {
    Serial.printf("OTA Error: %s\n", Update.errorString());
  }

  binHttp.end();
}


void handleRoot() {
  String publicIP = getPublicIP();

  String html = "<html><head><title>ESP32 OTA</title>"
                "<style>"
                "body{font-family:sans-serif;background:#f2f2f2;text-align:center;margin-top:50px;}"
                "</style></head><body>"
                "<h1>ESP32 OTA</h1>"
                "<p><b>Firmware:</b> " + String(firmware_version) + "</p>"
                "<p><b>WiFi:</b> " + WiFi.SSID() + "</p>"
                "<p><b>Local IP:</b> " + WiFi.localIP().toString() + "</p>"
                "<p><b>Public IP:</b> " + publicIP + "</p>"
                "</body></html>";

  server.send(200, "text/html", html);
}

String getPublicIP() {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://api.ipify.org");
  int httpCode = http.GET();
  String ip = "";
  if (httpCode == HTTP_CODE_OK) {
    ip = http.getString();
    ip.trim();
  }
  http.end();
  return ip;
}
