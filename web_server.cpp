#include "web_server.h"
#include "config.h"
#include "file_manager.h"
#include "dns_manager.h"
#include "secrets.h"

WebServerManager webServerManager;

void WebServerManager::handleRoot() {
  String html = fileManager.readFile("/index.html");
  if (html == "") {
    server.send(404, "text/plain", "File not found " + String(firmware_version));
    return;
  }

  html.replace("{{FIRMWARE_VERSION}}", firmware_version);
  html.replace("{{WIFI_SSID}}", WiFi.SSID());
  html.replace("{{LOCAL_IP}}", WiFi.localIP().toString());
  html.replace("{{PUBLIC_IP}}", dnsManager.getPublicIP());
  html.replace("{{CF_TOKEN}}", configManager.CF_TOKEN);
  html.replace("{{CF_ZONE}}", configManager.CF_ZONE);
  html.replace("{{CF_RECORD}}", configManager.CF_RECORD);
  html.replace("{{CF_HOST}}", configManager.CF_HOST);

  server.send(200, "text/html", html);
}

void WebServerManager::handleSaveConfig() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method not allowed");
    return;
  }

  configManager.CF_TOKEN = server.arg("cf_token");
  configManager.CF_ZONE = server.arg("cf_zone");
  configManager.CF_RECORD = server.arg("cf_record");
  configManager.saveConfig();

  server.send(200, "text/html", "<html><body><h2>Settings saved</h2><a href='/'>Return</a></body></html>");
}

void WebServerManager::setupRoutes() {
  server.on("/", HTTP_GET, []() { webServerManager.handleRoot(); });
  server.on("/save", HTTP_POST, []() { webServerManager.handleSaveConfig(); });
  server.begin();
}

void WebServerManager::handleClient() {
  server.handleClient();
}