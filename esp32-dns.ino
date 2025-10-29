#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "secrets.h"

// Configurações OTA
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";
const unsigned long checkInterval = 10 * 60 * 1000; // 10 minutos
unsigned long lastCheck = 0;

// Firmware atual
const char* firmware_version = "v1.0.0";

// Servidor web
WebServer server(80);

void setup() {
  // Serial confiável desde o boot
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(500);

  Serial.println("=== Inicializando ESP32 OTA com rollback ===");

  pinMode(LED_PIN, OUTPUT);

  // Checa se firmware atual é novo e marca como válido
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_NEW) {
      Serial.println("Novo firmware detectado. Confirmando imagem...");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  // Conecta ao WiFi
  Serial.println("Conectando ao WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());

  // Rotas web
  server.on("/", handleRoot);
  server.begin();

  // Checa atualização inicial
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
    Serial.println("WiFi não conectado. Abortando OTA.");
    return;
  }

  Serial.println("Checando GitHub API para atualização...");

  WiFiClientSecure client;
  client.setInsecure(); // Ignora SSL
  HTTPClient http;

  http.begin(client, github_api);
  http.addHeader("User-Agent", "ESP32");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Falha ao acessar API GitHub. Código: %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  // Parse do JSON
  DynamicJsonDocument doc(16384); // tamanho maior para payload GitHub
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("Erro ao parsear JSON: %s\n", error.c_str());
    return;
  }

  String latestVersion = doc["tag_name"];
  if (latestVersion == firmware_version) {
    Serial.println("Firmware já está atualizado.");
    return;
  }

  JsonArray assets = doc["assets"];
  if (assets.size() == 0) {
    Serial.println("Nenhum binário encontrado na release.");
    return;
  }

  String binUrl = assets[0]["browser_download_url"];
  Serial.printf("Nova release encontrada: %s\n", latestVersion.c_str());
  Serial.printf("Baixando binário de: %s\n", binUrl.c_str());

  // OTA segura
  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    Serial.println("Erro ao obter partição OTA de destino.");
    return;
  }

  HTTPClient binHttp;
  binHttp.begin(client, binUrl);
  int binCode = binHttp.GET();

  if (binCode != HTTP_CODE_OK) {
    Serial.printf("Falha ao baixar binário. Código: %d\n", binCode);
    binHttp.end();
    return;
  }

  int contentLength = binHttp.getSize();
  WiFiClient* stream = binHttp.getStreamPtr();

  if (contentLength <= 0) {
    Serial.println("Conteúdo do binário vazio. Abortando OTA.");
    binHttp.end();
    return;
  }

  Serial.printf("Iniciando OTA. Tamanho do binário: %d bytes\n", contentLength);
  if (!Update.begin(contentLength, U_FLASH, update_partition->address)) {
    Serial.printf("Falha ao iniciar OTA: %s\n", Update.errorString());
    binHttp.end();
    return;
  }

  size_t written = Update.writeStream(*stream);
  if (Update.end(true)) { // true = reinicia automaticamente
    if (Update.isFinished()) {
      Serial.println("Atualização OTA concluída com sucesso!");
      // firmware_version será atualizado no próximo boot
    } else {
      Serial.println("Update não finalizado corretamente.");
    }
  } else {
    Serial.printf("Erro OTA: %s\n", Update.errorString());
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
