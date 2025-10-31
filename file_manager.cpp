#include "file_manager.h"
#include <esp_partition.h>

FileManager fileManager;

bool FileManager::initLittleFS() {
  if (!LittleFS.begin(false)) {
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

void FileManager::listFiles() {
  Serial.println("Listing LittleFS files:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file){
    Serial.println(file.name());
    file = root.openNextFile();
  }
}

void FileManager::printPartitionUsage() {
  Serial.println("\n=== Partition Usage ===");

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

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) {
    Serial.printf("\nRunning App Partition: %s\n", running->label);
    Serial.printf("Address: 0x%06x | Size: %d bytes\n", running->address, running->size);
  }

  if (LittleFS.begin()) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.printf("\nLittleFS: Used %d / %d bytes (%.2f%%)\n", used, total, (100.0 * used / total));
  } else {
    Serial.println("LittleFS not mounted.");
  }

  Serial.println("==========================\n");
}

bool FileManager::writeFile(const String& path, const String& content) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing: " + path);
    return false;
  }

  size_t bytesWritten = file.write((const uint8_t*)content.c_str(), content.length());
  file.close();

  if (bytesWritten == content.length()) {
    Serial.printf("File %s updated successfully. Wrote %d bytes\n", path.c_str(), bytesWritten);
    return true;
  } else {
    Serial.printf("Failed to write file %s. Expected: %d, Written: %d\n", path.c_str(), content.length(), bytesWritten);
    return false;
  }
}

String FileManager::readFile(const String& path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    return "";
  }

  String content = "";
  while (file.available()) {
    content += char(file.read());
  }
  file.close();
  return content;
}