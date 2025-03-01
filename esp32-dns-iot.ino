#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BleKeyboard.h>

// Bluetooth config
BleKeyboard bleKeyboard("ESP32_Keyboard", "ESP32", 100);

// Configurações Wi-Fi
const char* ssid = "";  // Wifi SSID
const char* password = ""; // Wifi Password

// Configurações MQTT
const char* mqtt_server = ""; // mqtt server
const char* topic_request = "esp32/request";
const char* topic_response = "esp32/response";

// Configurações Cloudflare
const char* token = ""; // Cloudflare TOKEN
const char* zone_id = ""; // Zone ID
const char* records[] = { }; // IDs from records to update
const int num_records = 0;  // number of records ids

// Variáveis globais
String last_ip = "";
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Conecta ao Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi conectado");

  // Bluetooth Keyboard
  bleKeyboard.begin();

  // Configura o cliente MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Conecta ao MQTT
  reconnect();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Verifica o IP público a cada 60 segundos
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 60000) {
    lastCheck = millis();

    if (WiFi.status() == WL_CONNECTED) {
      String current_ip = getCurrentIP();
      if (current_ip != "" && current_ip != last_ip) {
        Serial.println("Novo IP detectado: " + current_ip);
        last_ip = current_ip;

        // Atualiza os registros DNS no Cloudflare
        for (int i = 0; i < num_records; i++) {
          updateDNSRecord(records[i], current_ip);
        }
      } else {
        Serial.println("IP não mudou: " + current_ip);
      }
    } else {
      Serial.println("WiFi desconectado");
    }
  }
}

// Função para obter o IP público
String getCurrentIP() {
  HTTPClient http;
  http.begin("https://api.ipify.org?format=json");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      const char* ip = doc["ip"];
      return String(ip);
    } else {
      Serial.println("Erro ao processar JSON.");
    }
  } else {
    Serial.printf("Erro na requisição HTTP: %s\n", http.errorToString(httpCode));
  }

  http.end();
  return "";
}

// Função para atualizar um registro DNS no Cloudflare
void updateDNSRecord(const char* record_id, String new_ip) {
  HTTPClient http;
  String url = "https://api.cloudflare.com/client/v4/zones/" + String(zone_id) + "/dns_records/" + String(record_id);

  http.begin(url);
  http.addHeader("Authorization", "Bearer " + String(token));
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"content\":\"" + new_ip + "\"}";
  int httpCode = http.PATCH(payload);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.println("Registro " + String(record_id) + " atualizado com sucesso.");
    Serial.println("Resposta: " + response);
  } else {
    Serial.printf("Erro ao atualizar registro %s: %s\n", record_id, http.errorToString(httpCode));
  }

  http.end();
}

// Função de callback para mensagens MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (msg == "IP") {
    client.publish(topic_response, WiFi.localIP().toString().c_str());
  } else if (msg == "REBOOT") {
    client.publish(topic_response, "ESP32: Reboot command received");
    ESP.restart();
  }
}

// Função para reconectar ao MQTT
void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conectar ao MQTT...");
    if (client.connect("ESP32Client")) {
      Serial.println("conectado");
      client.subscribe(topic_request);
    } else {
      Serial.print("falhou, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 5 segundos");
      delay(5000);
    }
  }
}