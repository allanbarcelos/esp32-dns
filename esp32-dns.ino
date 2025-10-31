#include <WiFi.h>
#include <EEPROM.h>
#include <esp_ota_ops.h>

#include "secrets.h"
#include "config.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include "dns_manager.h"
#include "file_manager.h"
#include "web_server.h"

// ----------------------------
// GLOBAL VARS
// ----------------------------
unsigned long lastIpPrint = 0;
const unsigned long ipPrintInterval = 60000UL;

// ----------------------------
// SETUP
// ----------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(500);

  Serial.println("=== Initializing ESP32 OTA with rollback ===");

  // Inicializar EEPROM
  EEPROM.begin(512);

  // Confirm new firmware if necessary
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_NEW) {
      Serial.println("New firmware detected. Confirming image...");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  // Initialize systems
  wifiManager.connect();
  configManager.loadConfig();
  
  if (!fileManager.initLittleFS()) {
    Serial.println("Failed to initialize LittleFS. Web interface will not work.");
  }
  
  webServerManager.setupRoutes();

  // Initial OTA check
  otaManager.checkForUpdate();
}

// ----------------------------
// MAIN LOOP
// ----------------------------
void loop() {
  webServerManager.handleClient();
  unsigned long now = millis();

  wifiManager.handleWiFi();

  // OTA check - chamar periodicamente
  static unsigned long lastOtaCheck = 0;
  const unsigned long otaCheckInterval = 10 * 60 * 1000UL; // 10 minutes
  if (now - lastOtaCheck > otaCheckInterval) {
    otaManager.checkForUpdate();
    lastOtaCheck = now;
  }

  // Daily reboot (non-blocking)
  static unsigned long bootTime = millis();
  if (now - bootTime > 86400000UL) {
    Serial.println("Daily reboot!");
    ESP.restart();
  }

  // DNS update
  if (dnsManager.shouldUpdateDNS()) {
    dnsManager.handleDNSUpdate();
  }

  // Print system info periodically
  if (now - lastIpPrint >= ipPrintInterval) {
    Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());
    fileManager.printPartitionUsage();
    lastIpPrint = now;
  }
}