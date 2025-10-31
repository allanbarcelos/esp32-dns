#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <LittleFS.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "secrets.h"

// ----------------------------
// Prefs
// ----------------------------
Preferences prefs; 

// ----------------------------
// CONFIG STRUCT
// ----------------------------
struct Config {
  char cf_token[64];
  char cf_zone[32];
  char cf_record[32];
  char cf_host[64];
};

Config config;

// ----------------------------
// OTA SETTINGS
// ----------------------------
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";
const unsigned long otaCheckInterval = 10 * 60 * 1000UL; // 10 minutes
unsigned long lastOtaCheck = 0;

const char* html_raw_url = "https://raw.githubusercontent.com/allanbarcelos/esp32-dns/main/data/index.html";

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

String CF_TOKEN = "";
String CF_ZONE = "";
String CF_RECORD = "";
String CF_HOST = "";

// ----------------------------
// PING / GET PERIODIC REQUEST
// ----------------------------
const unsigned long getInterval = 15000UL; // 15 segundos
unsigned long lastGetTime = 0;


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

  // Initialize LittleFS with format on failure
  if (!initLittleFS()) {
    Serial.println("Failed to initialize LittleFS. Web interface will not work.");
    // Continue without LittleFS - basic functionality will still work
  }
  
  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
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
    // listFiles();
    printPartitionUsage();
    lastIpPrint = now;
  }

  // Periodic GET request every 15 seconds
  if (now - lastGetTime >= getInterval) {
    lastGetTime = now;
    periodicGet();
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

  // Start OTA
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
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("Binary content empty.");
    http.end();
    return;
  }

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    Serial.println("OTA partition error.");
    http.end();
    return;
  }

  Serial.printf("Starting OTA. Size: %d bytes\n", contentLength);
  if (!Update.begin(contentLength, U_FLASH, update_partition->address)) {
    Serial.printf("OTA begin failed: %s\n", Update.errorString());
    http.end();
    return;
  }

  // Write in chunks
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
          return;
        }
        written += c;
        Serial.printf("OTA progress: %d/%d bytes\n", written, contentLength);
      }
    }
  }

  if (Update.end(true)) {
    if (Update.isFinished()) {
        Serial.println("OTA update completed successfully!");
        // Update HTML file before rebooting
        if (updateHTMLFromGitHub()) {
          Serial.println("HTML file updated successfully!");
        } else {
          Serial.println("Failed to update HTML file, but firmware update was successful");
        }
        Serial.println("Rebooting...");
        ESP.restart(); 
    } else {
        Serial.println("OTA did not finish correctly.");
    }
  } else {
      Serial.printf("OTA end error: %s\n", Update.errorString());
  }


  http.end();
}


// ----------------------------
// WEB SERVER HANDLERS
// ----------------------------

void handleRoot() {

  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found " + String(firmware_version));
    return;
  }

  String html = "";
  while (file.available()) {
    html += char(file.read());
  }
  file.close();

  // Substituir placeholders
  html.replace("{{FIRMWARE_VERSION}}", firmware_version);
  html.replace("{{WIFI_SSID}}", WiFi.SSID());
  html.replace("{{LOCAL_IP}}", WiFi.localIP().toString());
  html.replace("{{PUBLIC_IP}}", getPublicIP());
  html.replace("{{CF_TOKEN}}", CF_TOKEN);
  html.replace("{{CF_ZONE}}", CF_ZONE);
  html.replace("{{CF_RECORD}}", CF_RECORD);
  html.replace("{{CF_HOST}}", CF_HOST);

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
  CF_HOST = server.arg("cf_host");

  saveConfig();

  server.send(200, "text/html", "<html><body><h2>Settings saved</h2><a href='/'>Return</a></body></html>");
}

// ----------------------------
// CONFIG STORAGE
// ----------------------------
void loadConfig() {
  prefs.begin("myConfig", true);
  CF_TOKEN = prefs.getString("cf_token", "");
  CF_ZONE = prefs.getString("cf_zone", "");
  CF_RECORD = prefs.getString("cf_record", "");
  CF_HOST = prefs.getString("cf_host", "");
  prefs.end();

  if (CF_TOKEN.isEmpty()) CF_TOKEN = cf_token;
  if (CF_ZONE.isEmpty())  CF_ZONE  = cf_zone;
  if (CF_RECORD.isEmpty()) CF_RECORD = cf_record;
  if (CF_HOST.isEmpty())  CF_HOST  = cf_host;
}

void saveConfig() {
  prefs.begin("myConfig", false);
  prefs.putString("cf_token", CF_TOKEN);
  prefs.putString("cf_zone", CF_ZONE);
  prefs.putString("cf_record", CF_RECORD);
  prefs.putString("cf_host", CF_HOST);
  prefs.end();
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

void dnsUpdate(const String &ip) {
  if (CF_ZONE == "" || CF_RECORD == "" || CF_HOST == "") {
    Serial.println("DNS config missing.");
    return;
  }

  String url = "https://api.cloudflare.com/client/v4/zones/" + CF_ZONE + "/dns_records/" + CF_RECORD;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + CF_TOKEN);
  http.addHeader("Content-Type", "application/json");

  // Cloudflare expects full record JSON
  String payload = "{\"type\":\"A\",\"name\":\"" + CF_HOST + "\",\"content\":\"" + ip + "\",\"ttl\":1,\"proxied\":false}";
  int code = http.PATCH(payload);

  if (code > 0) {
    String resp = http.getString();
    if (resp.indexOf("\"success\":true") >= 0) {
      Serial.println("DNS successfully updated!");
    } else {
      Serial.println("Failed to update DNS. Response: " + resp);
    }
  } else {
    Serial.println("Error connecting to Cloudflare. HTTP code: " + String(code));
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

    case WIFI_DISCONNECTED:
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


bool initLittleFS() {
  if (!LittleFS.begin(false)) { // Don't format if mount fails
    Serial.println("LittleFS mount failed. Attempting to format...");
    if (LittleFS.format()) {
      Serial.println("LittleFS formatted successfully.");
      if (LittleFS.begin()) {
        Serial.println("LittleFS mounted after format.");
        return true;
      }
    }
    Serial.println("LittleFS format failed.");
    return false;
  }
  Serial.println("LittleFS mounted successfully.");
  return true;
}

void listFiles() {
  Serial.println("Listing LittleFS files:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file){
    Serial.println(file.name());
    file = root.openNextFile();
  }
}

void printPartitionUsage() {
  Serial.println("\n=== Partition Usage ===");

  // Lista todas as partições
  const esp_partition_t* partition = nullptr;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

  while (it != NULL) {
    partition = esp_partition_get(it);
    if (partition) {
      Serial.printf("Name: %-12s | Type: %d | Subtype: %02x | Address: 0x%06x | Size: %6d bytes\n",
                    partition->label, partition->type, partition->subtype,
                    partition->address, partition->size);
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);

  // -----------------------------
  // APP (firmware) partition info
  // -----------------------------
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    Serial.printf("\nRunning App Partition: %s\n", running->label);
    Serial.printf("Address: 0x%06x | Size: %d bytes\n", running->address, running->size);
  }

  // -----------------------------
  // LittleFS usage
  // -----------------------------
  if (LittleFS.begin()) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.printf("\nLittleFS: Used %d / %d bytes (%.2f%%)\n", used, total, (100.0 * used / total));
  } else {
    Serial.println("LittleFS not mounted.");
  }

  Serial.println("==========================\n");
}


// ----------------------------
// HTML UPDATE FUNCTION
// ----------------------------
bool updateHTMLFromGitHub() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Cannot update HTML.");
    return false;
  }

  Serial.println("Downloading HTML from GitHub...");
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

  // Open file for writing (truncate existing)
  File file = LittleFS.open("/index.html", "w");
  if (!file) {
    Serial.println("Failed to open index.html for writing");
    http.end();
    return false;
  }

  // Get the HTML content
  String htmlContent = http.getString();
  http.end();

  // Write to file
  size_t bytesWritten = file.write((const uint8_t*)htmlContent.c_str(), htmlContent.length());
  file.close();

  if (bytesWritten == htmlContent.length()) {
    Serial.printf("HTML file updated successfully. Wrote %d bytes\n", bytesWritten);
    return true;
  } else {
    Serial.printf("Failed to write HTML file. Expected: %d, Written: %d\n", htmlContent.length(), bytesWritten);
    return false;
  }
}

void periodicGet() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Skipping GET.");
    return;
  }

  WiFiClient client;
  HTTPClient http;
  String url = "http://158.69.220.0";

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.printf("GET %s -> HTTP %d\n", url.c_str(), httpCode);
  } else {
    Serial.printf("GET %s failed, error: %s\n", url.c_str(), http.errorToString(httpCode).c_str());
  }

  http.end();
}
