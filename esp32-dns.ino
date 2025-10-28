#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <Preferences.h>
#include "secrets.h"
#include "crypto.h"

// ===================== CONFIGURAÇÃO =====================
const char* github_api = "https://api.github.com/repos/allanbarcelos/esp32-dns/releases/latest";

// ===================== VARIÁVEIS GLOBAIS =====================
WebServer server(80);
Preferences preferences;

unsigned long checkInterval = 3600000UL;      // 1 hora
unsigned long dnsUpdateInterval = 300000UL;   // 5 minutos
unsigned long reconnectDelay = 5000;          // 5 segundos
const int maxReconnectAttempts = 5;
const int maxRebootsBeforeWait = 3;
const unsigned long waitAfterFails = 1800000UL; // 30 minutos

int rebootFailCount = 0;
unsigned long lastCheck = 0;
unsigned long dnsLastUpdate = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;
unsigned long dailyRebootTimer = 0;

enum WifiConnState { WIFI_OK, WIFI_RECONNECTING, WIFI_WAIT };
WifiConnState wifiState = WIFI_OK;
unsigned long waitStart = 0;

// ===================== CONFIGURAÇÕES NVS =====================
String CF_TOKEN, CF_ZONE, CF_RECORD, CF_HOST;

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

void saveRebootCount(int count) {
  preferences.begin("system", false);
  preferences.putInt("rebootFail", count);
  preferences.end();
}

int loadRebootCount() {
  preferences.begin("system", true);
  int count = preferences.getInt("rebootFail", 0);
  preferences.end();
  return count;
}

void loadAllConfigs() {
  CF_TOKEN  = loadConfig("CF_TOKEN", "");
  CF_ZONE   = loadConfig("CF_ZONE", "");
  CF_RECORD = loadConfig("CF_RECORD", "");
  CF_HOST   = loadConfig("CF_HOST", "");
}

// ===================== PÁGINA WEB =====================
void handleRoot() {
  String html = R"(
<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32 DNS</title>
<style>body{font-family:Arial;margin:40px;background:#f4f4f4;}
.container{max-width:600px;margin:auto;background:white;padding:20px;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.1);}
h1{color:#333;} input[type=text]{width:100%;padding:8px;margin:8px 0;}
input[type=submit]{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;}
</style></head><body>
<div class='container'><h1>ESP32 DNS + OTA</h1>
<p><a href='/config'>Configurar Cloudflare</a></p>
<p>Status: <strong>Online</strong></p>
</div></body></html>)";
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

  String html = R"(
<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Config</title>
<style>body{font-family:Arial;margin:40px;background:#f4f4f4;}
.container{max-width:600px;margin:auto;background:white;padding:20px;border-radius:10px;}
input[type=text]{width:100%;padding:8px;margin:8px 0;}
input[type=submit]{background:#28a745;color:white;padding:10px;border:none;border-radius:5px;}
</style></head><body>
<div class='container'>
<h1>Configuração Cloudflare</h1>
<form method='POST'>
<label>CF_TOKEN:</label><input type='text' name='CF_TOKEN' value=')"; html += CF_TOKEN; html += R"('><br>
<label>CF_ZONE:</label><input type='text' name='CF_ZONE' value=')"; html += CF_ZONE; html += R"('><br>
<label>CF_RECORD:</label><input type='text' name='CF_RECORD' value=')"; html += CF_RECORD; html += R"('><br>
<label>CF_HOST:</label><input type='text' name='CF_HOST' value=')"; html += CF_HOST; html += R"('><br>
<input type='submit' value='Salvar'>
</form>
</div></body></html>)";
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
        saveRebootCount(rebootFailCount);
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
          saveRebootCount(rebootFailCount);

          if (rebootFailCount >= maxRebootsBeforeWait) {
            Serial.println("Muitas falhas, entrando em modo de espera...");
            wifiState = WIFI_WAIT;
            waitStart = millis();
          } else {
            Serial.println("Falha total, reiniciando...");
            delay(1000);
            ESP.restart();
          }
        }
      }
      break;

    case WIFI_WAIT:
      if (now - waitStart >= waitAfterFails) {
        Serial.println("Tempo de espera finalizado, tentando novamente...");
        rebootFailCount = 0;
        saveRebootCount(rebootFailCount);
        WiFi.begin(ssid, password);
        wifiState = WIFI_RECONNECTING;
      }
      break;
  }
}

// ===================== DNS =====================
void dnsUpdate(String ip) {
  if (CF_TOKEN.isEmpty() || CF_ZONE.isEmpty() || CF_RECORD.isEmpty()) {
    Serial.println("Configuração Cloudflare incompleta.");
    return;
  }

  String url = "https://api.cloudflare.com/client/v4/zones/" + CF_ZONE + "/dns_records/" + CF_RECORD;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, url)) return;

  http.addHeader("Authorization", "Bearer " + CF_TOKEN);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"content\":\"" + ip + "\"}";

  int code = http.PATCH(payload);
  if (code > 0) {
    String resp = http.getString();
    if (resp.indexOf("\"success\":true") >= 0)
      Serial.println("DNS atualizado com sucesso!");
    else
      Serial.println("Falha ao atualizar DNS: " + resp);
  } else {
    Serial.println("Erro HTTP: " + String(code));
  }
  http.end();
}

String getPublicIP() {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, "http://api.ipify.org")) return "";
  int code = http.GET();
  String ip = (code == HTTP_CODE_OK) ? http.getString() : "";
  ip.trim();
  http.end();
  return ip;
}

String getDNSHostIP(String host) {
  IPAddress ip;
  return WiFi.hostByName(host.c_str(), ip) ? ip.toString() : "";
}

void handleDNSUpdate() {
  String publicIP = getPublicIP();
  if (publicIP.isEmpty()) return;

  String currentDNSIP = getDNSHostIP(CF_HOST);
  if (currentDNSIP.isEmpty()) return;

  if (currentDNSIP != publicIP) {
    Serial.println("Atualizando DNS: " + publicIP);
    dnsUpdate(publicIP);
  } else {
    Serial.println("DNS já atualizado.");
  }
}

// ===================== OTA =====================
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, github_api)) {
    Serial.println("Falha ao iniciar HTTP");
    return;
  }

  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("GitHub API erro: %d\n", code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  int idx = payload.indexOf("\"tag_name\"");
  if (idx < 0) return;

  int start = payload.indexOf("\"", idx + 11) + 1;
  int end = payload.indexOf("\"", start);
  String latest = payload.substring(start, end);

  if (latest == firmware_version) {
    Serial.println("Firmware atualizado.");
    return;
  }

  idx = payload.indexOf("\"browser_download_url\"");
  if (idx < 0) return;

  start = payload.indexOf("https://", idx);
  end = payload.indexOf("\"", start);
  String url = payload.substring(start, end);

  Serial.println("Baixando: " + latest);

  WiFiClientSecure binClient;
  binClient.setInsecure();
  HTTPClient binHttp;
  if (!binHttp.begin(binClient, url)) return;

  code = binHttp.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Download falhou: %d\n", code);
    binHttp.end();
    return;
  }

  int len = binHttp.getSize();
  if (len <= 0 || !Update.begin(len)) {
    Serial.println("Espaço insuficiente ou tamanho inválido");
    binHttp.end();
    return;
  }

  WiFiClient* stream = binHttp.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;

  while (written < len && binHttp.connected()) {
    size_t avail = stream->available();
    if (!avail) { delay(1); continue; }
    size_t read = stream->readBytes(buf, min(sizeof(buf), (size_t)(len - written)));
    if (read <= 0) break;

    decryptBuffer(buf, read);
    size_t w = Update.write(buf, read);
    if (w != read) {
      Serial.println("Erro ao gravar update");
      break;
    }
    written += w;
  }

  binHttp.end();

  if (written == len && Update.end(true)) {
    Serial.println("OTA concluído! Reiniciando...");
    ESP.restart();
  } else {
    Serial.printf("OTA falhou: %s\n", Update.errorString());
  }
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(100);

  rebootFailCount = loadRebootCount();

  loadAllConfigs();

  if (rebootFailCount >= maxRebootsBeforeWait) {
    wifiState = WIFI_WAIT;
    waitStart = millis();
  } else {
    WiFi.setHostname("ESP32_DNS");
    WiFi.begin(ssid, password);
    wifiState = WIFI_RECONNECTING;
  }

  Serial.printf("IP Local: %d\n",  WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/config", handleConfigPage);
  server.begin();

  dailyRebootTimer = millis();
  Serial.println("Sistema iniciado.");
}

void loop() {
  server.handleClient();
  handleWiFi();

  unsigned long now = millis();

  // Reboot diário (a cada ~24h)
  if (now - dailyRebootTimer >= 86400000UL) {
    Serial.println("Reboot diário agendado...");
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