#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "secrets.h"

// URL da última release
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";

// Intervalo para checar atualizações (10 minutos)
const unsigned long checkInterval = 10 * 60 * 1000;
unsigned long lastCheck = 0;

// Servidor web
WebServer server(80);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== Inicializando ESP32 OTA com rollback ===");

  // Confirma firmware atual se o boot foi bem-sucedido
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_NEW) {
      Serial.println("Novo firmware detectado. Confirmando imagem...");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  Serial.println("Conectando ao WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado ao WiFi!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Rotas web
  server.on("/", handleRoot);
  server.begin();

  // Checa por atualização inicial
  checkForUpdate();
  lastCheck = millis();
}

void loop() {
  server.handleClient();
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
  client.setInsecure();
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

  // Parse do JSON
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

  JsonArray assets = doc["assets"];
  if (assets.size() == 0) {
    Serial.println("Nenhum binário encontrado na release.");
    return;
  }

  String binUrl = assets[0]["browser_download_url"];
  Serial.println("Nova release encontrada: " + latestVersion);
  Serial.println("Baixando de: " + binUrl);

  // Baixa e aplica atualização OTA
  HTTPClient binHttp;
  binHttp.begin(client, binUrl);
  int binCode = binHttp.GET();

  if (binCode == HTTP_CODE_OK) {
    int contentLength = binHttp.getSize();
    WiFiClient* stream = binHttp.getStreamPtr();

    if (contentLength > 0) {
      Serial.printf("Tamanho do firmware: %d bytes\n", contentLength);

      if (Update.begin(contentLength)) {
        size_t written = Update.writeStream(*stream);
        if (written == contentLength) {
          Serial.println("Gravação concluída. Finalizando...");
          if (Update.end()) {
            if (Update.isFinished()) {
              Serial.println("Atualização gravada com sucesso.");

              // Define nova partição de boot
              const esp_partition_t* newPartition = esp_ota_get_next_update_partition(NULL);
              esp_err_t err = esp_ota_set_boot_partition(newPartition);
              if (err == ESP_OK) {
                Serial.println("Partição OTA configurada. Reiniciando...");
                ESP.restart();
              } else {
                Serial.printf("Erro ao definir partição de boot: %s\n", esp_err_to_name(err));
              }
            } else {
              Serial.println("Update não finalizado corretamente.");
            }
          } else {
            Serial.printf("Erro na atualização: %s\n", Update.errorString());
          }
        } else {
          Serial.printf("Erro ao gravar firmware (%d/%d bytes).\n", written, contentLength);
        }
      } else {
        Serial.println("Falha ao iniciar Update OTA.");
      }
    } else {
      Serial.println("Conteúdo do binário vazio.");
    }
  } else {
    Serial.printf("Falha ao baixar binário. Código: %d\n", binCode);
  }

  binHttp.end();
}

void handleRoot() {
  String publicIP = getPublicIP();

  String html = "<html><head><title>ESP32 OTA</title>"
                "<style>"
                "body{font-family:sans-serif;background:#f2f2f2;text-align:center;margin-top:50px;}"
                "button{padding:10px 20px;font-size:16px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer;}"
                "button:hover{background:#0056b3;}"
                "</style></head><body>"
                "<h1>ESP32 OTA</h1>"
                "<p><b>Versão atual:</b> " + String(firmware_version) + "</p>"
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

String getDNSHostIP(String host) {
  IPAddress resolvedIP;
  if (WiFi.hostByName(host.c_str(), resolvedIP))
    return resolvedIP.toString();
  return "";
}
