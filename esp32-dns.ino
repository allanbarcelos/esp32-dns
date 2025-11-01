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
// PREFERENCES
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

    char wifi_ssid[32];
    char wifi_password[64];
};

Config config;

// ----------------------------
// OTA SETTINGS
// ----------------------------
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";
const unsigned long otaCheckInterval = 10 * 60 * 1000UL; // 10 min
unsigned long lastOtaCheck = 0;

// Files to update in LittleFS
struct RemoteFile {
    const char* url;
    const char* path;
};

RemoteFile files[] = {
    { "https://raw.githubusercontent.com/allanbarcelos/esp32-dns/main/data/index.html", "/index.html" }
};

// ----------------------------
// WIFI & RECONNECT SETTINGS
// ----------------------------
const unsigned long dnsUpdateInterval = 300000UL; // 5 min
const unsigned long reconnectDelay = 5000UL;      // 5 s
const int maxReconnectAttempts = 5;
const int maxRebootsBeforeWait = 3;
const unsigned long waitAfterFails = 1800000UL;  // 30 min

int rebootFailCount = 0;
unsigned long lastDnsUpdate = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;

enum WifiConnState_t { WIFI_OK, WIFI_RECONNECTING, WIFI_WAIT };
WifiConnState_t wifiState = WIFI_OK;
unsigned long waitStart = 0;

// ----------------------------
// GLOBAL VARS
// ----------------------------
// unsigned long lastIpPrint = 0;
// const unsigned long ipPrintInterval = 60000UL;

String CF_TOKEN = "";
String CF_ZONE = "";
String CF_RECORD = "";
String CF_HOST = "";

// ----------------------------
// PERIODIC REQUEST
// ----------------------------
const unsigned long getInterval = 300000UL; // 5 min
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
    while (!Serial) delay(10);
    delay(500);

    Serial.println("=== Initializing ESP32 OTA with rollback ===");

    // Confirm new firmware if necessary
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) == ESP_OK && otaState == ESP_OTA_IMG_NEW) {
        Serial.println("New firmware detected. Confirming image...");
        esp_ota_mark_app_valid_cancel_rollback();
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

    // Initialize LittleFS
    if (!initLittleFS()) {
        Serial.println("LittleFS failed. Web interface unavailable.");
    }

    // Web server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", handleSaveConfig);
    server.begin();

    // Initial OTA check
    // checkForUpdate();
    // lastOtaCheck = millis();
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

    // Daily reboot
    static unsigned long bootTime = millis();
    if (now - bootTime > 86400000UL) ESP.restart();

    // DNS update
    if (wifiState == WIFI_OK && (now - lastDnsUpdate >= dnsUpdateInterval || lastDnsUpdate == 0)) {
        lastDnsUpdate = now;
        handleDNSUpdate();
    }

    // Print IP/Partition periodically
    /* if (now - lastIpPrint >= ipPrintInterval) {
        Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());
        printPartitionUsage();
        lastIpPrint = now;
    } */

    // Periodic GET request
    if (now - lastGetTime >= getInterval) {
        lastGetTime = now;
        periodicGet();
    }
}

// ----------------------------
// OTA UPDATE
// ----------------------------
void checkForUpdate() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected. OTA aborted.");
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
    if (deserializeJson(doc, payload)) {
        Serial.println("JSON parse error.");
        return;
    }

    String latestVersion = doc["tag_name"];
    if (latestVersion == firmware_version) {
        Serial.println("Firmware is up-to-date.");
        return;
    }

    JsonArray assets = doc["assets"];
    if (assets.size() == 0) {
        Serial.println("No binary in release.");
        return;
    }

    String binUrl = assets[0]["browser_download_url"];
    Serial.printf("New release %s found. Downloading from %s\n", latestVersion.c_str(), binUrl.c_str());

    // OTA download and update
    http.begin(client, binUrl);
    http.addHeader("User-Agent", "ESP32");
    int binCode = http.GET();

    // Handle redirect
    if (binCode == HTTP_CODE_MOVED_PERMANENTLY || binCode == HTTP_CODE_FOUND) {
        String redirectUrl = http.getLocation();
        Serial.printf("Redirect detected. Following to: %s\n", redirectUrl.c_str());
        http.end();
        http.begin(client, redirectUrl);
        http.addHeader("User-Agent", "ESP32");
        binCode = http.GET();
    }

    if (binCode != HTTP_CODE_OK) {
        Serial.printf("Failed to download binary. HTTP %d\n", binCode);
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Binary empty.");
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

    if (Update.end(true) && Update.isFinished()) {
        Serial.println("OTA update completed successfully!");
        if (updateDistFiles()) Serial.println("Web files updated successfully!");
        else Serial.println("Web files update failed.");
        ESP.restart();
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

    String html;
    while (file.available()) html += char(file.read());
    file.close();

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
    server.send(200, "application/json", "{\"success\":true}");

}

// ----------------------------
// CONFIG STORAGE
// ----------------------------
void loadConfig() {
    prefs.begin("myConfig", true);
    CF_TOKEN = prefs.getString("cf_token", cf_token);
    CF_ZONE  = prefs.getString("cf_zone", cf_zone);
    CF_RECORD = prefs.getString("cf_record", cf_record);
    CF_HOST  = prefs.getString("cf_host", cf_host);
    prefs.end();
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
    String ip;
    if (httpCode == HTTP_CODE_OK) ip = http.getString().trim();
    http.end();
    return ip;
}

String getDNSHostIP(String host) {
    IPAddress resolvedIP;
    return WiFi.hostByName(host.c_str(), resolvedIP) ? resolvedIP.toString() : "";
}

void dnsUpdate(String ip) {
    String url = "https://api.cloudflare.com/client/v4/zones/" + CF_ZONE + "/dns_records/" + CF_RECORD;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + CF_TOKEN);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"content\":\"" + ip + "\"}";
    int code = http.PATCH(payload);
    if (code > 0) {
        String resp = http.getString();
        Serial.println(resp.indexOf("\"success\":true") >= 0 ? "DNS updated!" : "DNS update failed.");
    } else Serial.println("DNS update error: " + String(code));
    http.end();
}

void handleDNSUpdate() {
    String publicIP = getPublicIP();
    String currentDNSIP = getDNSHostIP(CF_HOST);
    if (!publicIP.isEmpty() && !currentDNSIP.isEmpty() && publicIP != currentDNSIP) {
        Serial.println("Updating DNS...");
        dnsUpdate(publicIP);
    } else Serial.println("DNS already up-to-date.");
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
                Serial.printf("Reconnect attempt %d/%d\n", reconnectAttempts, maxReconnectAttempts);
                WiFi.disconnect(); WiFi.begin(ssid, password);
                if (reconnectAttempts >= maxReconnectAttempts) {
                    rebootFailCount++;
                    EEPROM.write(0, rebootFailCount); EEPROM.commit();
                    if (rebootFailCount >= maxRebootsBeforeWait) {
                        Serial.println("Too many failures. Wait mode...");
                        wifiState = WIFI_WAIT;
                        waitStart = millis();
                    } else ESP.restart();
                }
            }
            break;
        case WIFI_WAIT:
            if (now - waitStart >= waitAfterFails) {
                Serial.println("Wait complete. Trying again...");
                rebootFailCount = 0; EEPROM.write(0, rebootFailCount); EEPROM.commit();
                WiFi.begin(ssid, password);
                wifiState = WIFI_RECONNECTING;
            }
            break;
    }
}

// ----------------------------
// FILE SYSTEM / LittleFS
// ----------------------------
bool initLittleFS() {
    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS mount failed. Formatting...");
        if (LittleFS.format() && LittleFS.begin()) {
            Serial.println("LittleFS mounted after format.");
            return true;
        }
        return false;
    }
    Serial.println("LittleFS mounted successfully.");
    return true;
}

bool mkdirRecursively(const String &path) {
    if (path.isEmpty() || path == "/") return true;
    if (LittleFS.exists(path)) return true;
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0 && !mkdirRecursively(path.substring(0, lastSlash))) return false;
    return LittleFS.mkdir(path) && Serial.printf("Created directory: %s\n", path.c_str());
}

bool downloadFileToLittleFS(const char* url, const char* path) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, url); http.addHeader("User-Agent", "ESP32");
    if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }

    int contentLength = http.getSize();
    if (contentLength <= 0) { http.end(); return false; }

    int lastSlash = String(path).lastIndexOf('/');
    if (lastSlash > 0 && !mkdirRecursively(String(path).substring(0, lastSlash))) { http.end(); return false; }

    File f = LittleFS.open(path, "w");
    if (!f) { http.end(); return false; }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[1024]; int written = 0;
    while (http.connected() && written < contentLength) {
        size_t len = stream->available();
        if (len) {
            if (len > sizeof(buffer)) len = sizeof(buffer);
            int c = stream->readBytes(buffer, len);
            if (c > 0) { f.write(buffer, c); written += c; }
        }
    }
    f.close(); http.end();
    Serial.printf("Saved %s (%d bytes)\n", path, written);
    return true;
}

bool updateDistFiles() {
    bool allSuccess = true;
    for (auto &file : files) if (!downloadFileToLittleFS(file.url, file.path)) allSuccess = false;
    return allSuccess;
}

// ----------------------------
// UTILITIES
// ----------------------------
/*
void printPartitionUsage() {
    Serial.println("\n=== Partition Usage ===");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) Serial.printf("Name: %-12s | Addr: 0x%06x | Size: %d\n", p->label, p->address, p->size);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) Serial.printf("Running App Partition: %s, Addr: 0x%06x, Size: %d\n", running->label, running->address, running->size);

    if (LittleFS.begin()) {
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        Serial.printf("LittleFS: Used %d / %d bytes (%.2f%%)\n", used, total, 100.0 * used / total);
    } else Serial.println("LittleFS not mounted.");
}
*/

// ----------------------------
// PERIODIC GET
// ----------------------------
void periodicGet() {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClient client; HTTPClient http;
    String url = "http://158.69.220.0";
    int code = http.begin(client, url).GET();
    Serial.printf("GET %s -> HTTP %d\n", url.c_str(), code);
    http.end();
}
