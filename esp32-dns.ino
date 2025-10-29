#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "secrets.h"

// URL da última release
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";

// Intervalo para checar atualizações (10 minutos)
const unsigned long checkInterval = 10 * 60 * 1000;
unsigned long lastCheck = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Conectando ao WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado ao WiFi!");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  checkForUpdate();
  lastCheck = millis();
}

void loop() {
  if (millis() - lastCheck > checkInterval) {
    checkForUpdate();
    lastCheck = millis();
  }
}

void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi não conectado.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Ignora certificado SSL
  HTTPClient http;

  http.begin(client, github_api);
  http.addHeader("User-Agent", "ESP32");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Falha ao acessar GitHub API. Código: %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  // Parse JSON com ArduinoJson
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.println("Erro ao parsear JSON da API.");
    return;
  }

  String latestVersion = doc["tag_name"];
  if (latestVersion == firmware_version) {
    Serial.println("Firmware já está atualizado.");
    return;
  }

  // Pega URL do binário (assume que o primeiro asset é o binário)
  JsonArray assets = doc["assets"];
  if (assets.size() == 0) {
    Serial.println("Nenhum binário encontrado na release.");
    return;
  }

  String binUrl = assets[0]["browser_download_url"];
  Serial.println("Nova release encontrada: " + latestVersion);
  Serial.println("Baixando de: " + binUrl);

  // Download do binário e atualização OTA
  HTTPClient binHttp;
  binHttp.begin(client, binUrl);
  int binCode = binHttp.GET();

  if (binCode == HTTP_CODE_OK) {
    int contentLength = binHttp.getSize();
    if (contentLength > 0) {
      if (Update.begin(contentLength)) {
        if (Update.writeStream(client) == contentLength) {
          if (Update.end(true)) { // true = reinicia automaticamente
            Serial.println("Atualização concluída com sucesso!");
          } else {
            Serial.printf("Erro na atualização: %s\n", Update.getErrorString().c_str());
          }
        } else {
          Serial.println("Erro ao escrever dados OTA.");
        }
      } else {
        Serial.println("Não foi possível iniciar atualização.");
      }
    } else {
      Serial.println("Conteúdo vazio no binário.");
    }
  } else {
    Serial.printf("Falha ao baixar binário. Código: %d\n", binCode);
  }

  binHttp.end();
}
