#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <EEPROM.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "secrets.h"

// ----------------------------
// CONFIG STRUCT
// ----------------------------
struct Config {
  char cf_token[64];
  char cf_zone[32];
  char cf_record[32];
};

Config config;

// ----------------------------
// OTA SETTINGS
// ----------------------------
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";
const unsigned long otaCheckInterval = 10 * 60 * 1000UL; // 10 minutes
unsigned long lastOtaCheck = 0;

// ----------------------------
// WIFI & RECONNECT SETTINGS
// ----------------------------
const unsigned long dnsUpdateInterval = 300000UL;   // 5 minutes
const unsigned long reconnectDelay = 5000UL;        // 5 seconds
const int maxReconnectAttempts = 5;
const int maxRebootsBeforeWait = 3;
const unsigned long waitAfterFails = 1800000UL;    // 30 minutes

int rebootFailCount = 0;
unsigned long lastDnsUpdate = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;

enum WifiConnState_t { WIFI_OK, WIFI_DISCONNECTED, WIFI_RECONNECTING, WIFI_WAIT };
WifiConnState_t wifiState = WIFI_OK;
unsigned long waitStart = 0;


// ----------------------------
// GLOBAL VARS
// ----------------------------
unsigned long lastIpPrint = 0;
const unsigned long ipPrintInterval = 60000UL; // 1 minute (60.000 ms)

// ----------------------------
// WEB SERVER
// ----------------------------
WebServer server(80);

// ----------------------------
// SETUP
// ----------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(500);

  Serial.println("=== Initializing ESP32 OTA with rollback ===");

  // Confirm new firmware if necessary
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
  Serial.printf("\nWiFi connected! Local IP: %s\n", WiFi.localIP().toString().c_str());

  // Load configuration
  loadConfig();

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/save", handleSaveConfig);
  server.begin();

  // Initial OTA check
  checkForUpdate();
  lastOtaCheck = millis();
}

// ----------------------------
// MAIN LOOP
// ----------------------------
void loop() {
  server.handleClient();
  unsigned long now = millis();

  handleWiFi();

  // OTA check
  if (now - lastOtaCheck > otaCheckInterval) {
    checkForUpdate();
    lastOtaCheck = now;
  }

  // Daily reboot (non-blocking)
  static unsigned long bootTime = millis();
  if (now - bootTime > 86400000UL) {
    Serial.println("Daily reboot!");
    ESP.restart();
  }

  // DNS update
  if (wifiState == WIFI_OK && (now - lastDnsUpdate >= dnsUpdateInterval || lastDnsUpdate == 0)) {
    lastDnsUpdate = now;
    handleDNSUpdate();
  }

  // Print local IP periodically
  if (now - lastIpPrint >= ipPrintInterval) {
    Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());
    lastIpPrint = now;
  }
}

// ----------------------------
// OTA UPDATE FUNCTIONS
// ----------------------------
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
  Serial.printf("New release found: %s\nDownloading binary from: %s\n", latestVersion.c_str(), binUrl.c_str());

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    Serial.println("Error getting OTA target partition.");
    return;
  }

  HTTPClient binHttp;
  binHttp.begin(client, binUrl);
  binHttp.addHeader("User-Agent", "ESP32");
  int binCode = binHttp.GET();

  // Handle redirect
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
    if (Update.isFinished()) Serial.println("OTA update completed successfully!");
    else Serial.println("Update did not finish correctly.");
  } else {
    Serial.printf("OTA Error: %s\n", Update.errorString());
  }

  binHttp.end();
}

// ----------------------------
// WEB SERVER HANDLERS
// ----------------------------
void handleRoot() {
  String publicIP = getPublicIP();

  String html = "<html><head><title>ESP32 OTA</title>"
                "<style>body{font-family:sans-serif;background:#f2f2f2;text-align:center;margin-top:50px;}</style></head><body>"
                "<h1>ESP32 OTA</h1>"
                "<p><b>Firmware:</b> " + String(firmware_version) + "</p>"
                "<p><b>WiFi:</b> " + WiFi.SSID() + "</p>"
                "<p><b>Local IP:</b> " + WiFi.localIP().toString() + "</p>"
                "<p><b>Public IP:</b> " + publicIP + "</p>"
                "<hr />"
                "<h2>DNS Settings</h2>"
                "<form method='POST' action='/save'>"
                "Cloudflare Token: <input name='cf_token' value='" + String(CF_TOKEN) + "'><br>"
                "Zone ID: <input name='cf_zone' value='" + String(CF_ZONE) + "'><br>"
                "Record ID: <input name='cf_record' value='" + String(CF_RECORD) + "'><br>"
                "<button type='submit'>Save</button>"
                "</body></html>";

  server.send(200, "text/html", html);
}

void handleSaveConfig() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }

  CF_TOKEN = server.arg("cf_token");
  CF_ZONE = server.arg("cf_zone");
  CF_RECORD = server.arg("cf_record");

  saveConfig();

  server.send(200, "text/html", "<html><body><h2>Settings saved</h2><a href='/'>Return</a></body></html>");
}

// ----------------------------
// CONFIG STORAGE
// ----------------------------
void loadConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, config);
  CF_TOKEN = String(config.cf_token);
  CF_ZONE = String(config.cf_zone);
  CF_RECORD = String(config.cf_record);
}

void saveConfig() {
  strncpy(config.cf_token, CF_TOKEN.c_str(), sizeof(config.cf_token));
  strncpy(config.cf_zone, CF_ZONE.c_str(), sizeof(config.cf_zone));
  strncpy(config.cf_record, CF_RECORD.c_str(), sizeof(config.cf_record));

  EEPROM.begin(sizeof(Config));
  EEPROM.put(0, config);
  EEPROM.commit();
}

// ----------------------------
// DNS FUNCTIONS
// ----------------------------
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

String getDNSHostIP(String host) {
  IPAddress resolvedIP;
  if (WiFi.hostByName(host.c_str(), resolvedIP)) return resolvedIP.toString();
  return "";
}

void dnsUpdate(String ip) {
  String url = "https://api.cloudflare.com/client/v4/zones/" + String(CF_ZONE) + "/dns_records/" + String(CF_RECORD);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + String(CF_TOKEN));
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"content\":\"" + ip + "\"}";
  int code = http.PATCH(payload);
  if (code > 0) {
    String resp = http.getString();
    if (resp.indexOf("\"success\":true") >= 0) Serial.println("DNS successfully updated!");
    else Serial.println("Failed to update DNS.");
  } else {
    Serial.println("Error updating DNS. Code: " + String(code));
  }
  http.end();
}

void handleDNSUpdate() {
  String publicIP = getPublicIP();
  if (publicIP == "") return;

  String currentDNSIP = getDNSHostIP(CF_HOST);
  if (currentDNSIP == "") return;

  if (currentDNSIP != publicIP) {
    Serial.println("Updating DNS...");
    dnsUpdate(publicIP);
  } else {
    Serial.println("DNS is already up-to-date.");
  }
}

// ----------------------------
// WIFI CONNECTION HANDLER
// ----------------------------
void handleWiFi() {
  unsigned long now = millis();

  switch (wifiState) {
    case WIFI_OK:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected.");
        wifiState = WIFI_RECONNECTING;
        reconnectAttempts = 0;
        lastReconnectAttempt = 0;
      }
      break;

    case WIFI_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Reconnected!");
        rebootFailCount = 0;
        EEPROM.write(0, rebootFailCount);
        EEPROM.commit();
        wifiState = WIFI_OK;
        break;
      }

      if (now - lastReconnectAttempt >= reconnectDelay) {
        lastReconnectAttempt = now;
        reconnectAttempts++;

        Serial.printf("Reconnect attempt %d/%d...\n", reconnectAttempts, maxReconnectAttempts);
        WiFi.disconnect();
        WiFi.begin(ssid, password);

        if (reconnectAttempts >= maxReconnectAttempts) {
          rebootFailCount++;
          EEPROM.write(0, rebootFailCount);
          EEPROM.commit();

          if (rebootFailCount >= maxRebootsBeforeWait) {
            Serial.println("Too many failures. Going to wait mode...");
            wifiState = WIFI_WAIT;
            waitStart = millis();
          } else {
            Serial.println("Total failure, restarting...");
            ESP.restart();
          }
        }
      }
      break;

    case WIFI_WAIT:
      if (now - waitStart >= waitAfterFails) {
        Serial.println("Wait time completed. Trying again...");
        rebootFailCount = 0;
        EEPROM.write(0, rebootFailCount);
        EEPROM.commit();
        WiFi.begin(ssid, password);
        wifiState = WIFI_RECONNECTING;
      }
      break;
  }
}
