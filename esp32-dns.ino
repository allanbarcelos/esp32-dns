#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Preferences.h>
#include "secrets.h"
#include "crypto.h"

// ===================== CONFIGURAÇÃO =====================
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";

// ===================== VARIÁVEIS =====================
WebServer server(80);
Preferences preferences;

unsigned long checkInterval = 3600000UL;      // 1 hora
unsigned long dnsUpdateInterval = 300000UL;   // 5 minutos
unsigned long reconnectDelay = 5000;          // 5 segundos entre tentativas
const int maxReconnectAttempts = 5;
const int maxRebootsBeforeWait = 3;
const unsigned long waitAfterFails = 1800000UL; // 30 minutos

int rebootFailCount = 0;
unsigned long lastCheck = 0;
unsigned long dnsLastUpdate = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;

enum WifiConnState { WIFI_OK, WIFI_RECONNECTING, WIFI_WAIT };
WifiConnState wifiState = WIFI_OK;
unsigned long waitStart = 0;

// ===================== CONFIGURAÇÕES NVS =====================
String CF_TOKEN;
String CF_ZONE;
String CF_RECORD;
String CF_HOST;

// ===================== FUNÇÕES NVS =====================
void saveConfig(const char* key, const String &value) {
  preferences.begin("config", false);
  preferences.putString(key, value);
  preferences.end();
}

String loadConfig(const char* key, const String &defaultVal = "") {
  preferences.begin("config", true);
  String val = preferences.getString(key, defaultVal);
  preferences.end();
  return val;
}

void loadAllConfigs() {
  CF_TOKEN  = loadConfig("CF_TOKEN", "");
  CF_ZONE   = loadConfig("CF_ZONE", "");
  CF_RECORD = loadConfig("CF_RECORD", "");
  CF_HOST   = loadConfig("CF_HOST", "");
}

// ===================== CONFIGURAÇÃO WEB =====================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32</title>"
                "<style>body{display:flex;justify-content:center;align-items:center;height:100vh;"
                "margin:0;font-family:Arial;}h1{font-size:3em;}</style></head>"
                "<body><h1>ESP32 Online</h1></body></html>";
  server.send(200, "text/html", html);
}

void handleConfigPage() {
  if (server.method() == HTTP_POST) {
    saveConfig("CF_TOKEN", server.arg("CF_TOKEN"));
    saveConfig("CF_ZONE", server.arg("CF_ZONE"));
    saveConfig("CF_RECORD", server.arg("CF_RECORD"));
    saveConfig("CF_HOST", server.arg("CF_HOST"));
    loadAllConfigs();
    server.send(200, "text/html", "<h1>Configurações salvas!</h1><a href='/'>Voltar</a>");
    return;
  }

  String tokenVal = CF_TOKEN; tokenVal.replace("\"", "&quot;");
  String zoneVal = CF_ZONE; zoneVal.replace("\"", "&quot;");
  String recordVal = CF_RECORD; recordVal.replace("\"", "&quot;");
  String hostVal = CF_HOST; hostVal.replace("\"", "&quot;");

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuração</title></head><body>";
  html += "<h1>Configuração Cloudflare</h1>";
  html += "<form method='POST'>";
  html += "CF_TOKEN: <input type='text' name='CF_TOKEN' value='" + tokenVal + "'><br>";
  html += "CF_ZONE: <input type='text' name='CF_ZONE' value='" + zoneVal + "'><br>";
  html += "CF_RECORD: <input type='text' name='CF_RECORD' value='" + recordVal + "'><br>";
  html += "CF_HOST: <input type='text' name='CF_HOST' value='" + hostVal + "'><br>";
  html += "<input type='submit' value='Salvar'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

// ===================== WIFI =====================
void handleWiFi() {
  unsigned long now = millis();

  switch (wifiState) {
    case WIFI_OK:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi desconectado.");
        wifiState = WIFI_RECONNECTING;
        reconnectAttempts = 0;
        lastReconnectAttempt = 0;
      }
      break;

    case WIFI_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi reconectado!");
        rebootFailCount = 0;
        EEPROM.write(0, rebootFailCount);
        EEPROM.commit();
        wifiState = WIFI_OK;
        break;
      }

      if (now - lastReconnectAttempt >= reconnectDelay) {
        lastReconnectAttempt = now;
        reconnectAttempts++;
        Serial.printf("Tentativa %d/%d...\n", reconnectAttempts, maxReconnectAttempts);
        WiFi.disconnect();
        WiFi.begin(ssid, password);

        if (reconnectAttempts >= maxReconnectAttempts) {
          rebootFailCount++;
          EEPROM.write(0, rebootFailCount);
          EEPROM.commit();

          if (rebootFailCount >= maxRebootsBeforeWait) {
            Serial.println("Muitas falhas, entrando em modo de espera...");
            wifiState = WIFI_WAIT;
            waitStart = millis();
          } else {
            Serial.println("Falha total, reiniciando...");
            ESP.restart();
          }
        }
      }
      break;

    case WIFI_WAIT:
      if (now - waitStart >= waitAfterFails) {
        Serial.println("Tempo de espera finalizado, tentando novamente...");
        rebootFailCount = 0;
        EEPROM.write(0, rebootFailCount);
        EEPROM.commit();
        WiFi.begin(ssid, password);
        wifiState = WIFI_RECONNECTING;
      }
      break;
  }
}

// ===================== DNS =====================
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
    if (resp.indexOf("\"success\":true") >= 0)
      Serial.println("DNS atualizado com sucesso!");
    else
      Serial.println("Falha ao atualizar DNS.");
  } else {
    Serial.println("Erro ao atualizar DNS. Código: " + String(code));
  }
  http.end();
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
  if (WiFi.hostByName(host.c_str(), resolvedIP)) return resolvedIP.toString();
  return "";
}

void handleDNSUpdate() {
  String publicIP = getPublicIP();
  if (publicIP == "") return;

  String currentDNSIP = getDNSHostIP(CF_HOST);
  if (currentDNSIP == "") return;

  if (currentDNSIP != publicIP) {
    Serial.println("Atualizando DNS...");
    dnsUpdate(publicIP);
  } else {
    Serial.println("DNS já está atualizado.");
  }
}

// ===================== OTA =====================
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;

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

  int idxVersion = payload.indexOf("\"tag_name\"");
  if (idxVersion < 0) return;

  int startVer = payload.indexOf("\"", idxVersion + 10) + 1;
  int endVer = payload.indexOf("\"", startVer);
  String latestVersion = payload.substring(startVer, endVer);

  if (latestVersion == firmware_version) {
    Serial.println("Firmware já está atualizado.");
    return;
  }

  int idx = payload.indexOf("\"browser_download_url\"");
  if (idx < 0) return;
  int start = payload.indexOf("https://", idx);
  int end = payload.indexOf("\"", start);
  String binUrl = payload.substring(start, end);

  Serial.println("Nova versão disponível: " + binUrl);

  WiFiClientSecure binClient;
  binClient.setInsecure();
  HTTPClient binHttp;
  binHttp.begin(binClient, binUrl);
  int binCode = binHttp.GET();

  if (binCode == HTTP_CODE_OK) {
    int contentLength = binHttp.getSize();
    if (Update.begin(contentLength)) {
      WiFiClient *stream = binHttp.getStreamPtr();
      uint8_t buf[1024];
      int bytesRead = 0;
      while (bytesRead < contentLength) {
        size_t toRead = min(sizeof(buf), (size_t)(contentLength - bytesRead));
        int c = stream->readBytes(buf, toRead);
        if (c <= 0) break;
        decryptBuffer(buf, c);
        Update.write(buf, c);
        bytesRead += c;
        yield();
      }
      if (Update.end()) {
        Serial.println("Atualização concluída!");
        ESP.restart();
      } else {
        Serial.printf("Erro na atualização: %s\n", Update.getErrorString());
      }
    }
  } else {
    Serial.printf("Falha ao baixar binário. Código: %d\n", binCode);
  }
  binHttp.end();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

  rebootFailCount = EEPROM.read(0);
  Serial.printf("Falhas anteriores: %d\n", rebootFailCount);

  loadAllConfigs();  // carrega CF_TOKEN, CF_ZONE, CF_RECORD, CF_HOST

  if (rebootFailCount >= maxRebootsBeforeWait) {
    wifiState = WIFI_WAIT;
    waitStart = millis();
  } else {
    WiFi.setHostname("ESP32_DNS");
    WiFi.begin(ssid, password);
    wifiState = WIFI_RECONNECTING;
  }

  server.on("/", handleRoot);
  server.on("/config", handleConfigPage);
  server.begin();
  Serial.println("Servidor HTTP iniciado!");
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();
  handleWiFi();

  unsigned long now = millis();

  // Reboot diário
  if (now > 86400000UL) {
    ESP.restart();
  }

  // OTA
  if (wifiState == WIFI_OK && (now - lastCheck >= checkInterval || lastCheck == 0)) {
    lastCheck = now;
    checkForUpdate();
  }

  // DNS
  if (wifiState == WIFI_OK && (now - dnsLastUpdate >= dnsUpdateInterval || dnsLastUpdate == 0)) {
    dnsLastUpdate = now;
    handleDNSUpdate();
  }
}
