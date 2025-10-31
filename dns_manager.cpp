#include "dns_manager.h"
#include "config.h"
#include "wifi_manager.h"

DNSManager dnsManager;

String DNSManager::getPublicIP() {
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

String DNSManager::getDNSHostIP(const String& host) {
  IPAddress resolvedIP;
  if (WiFi.hostByName(host.c_str(), resolvedIP)) return resolvedIP.toString();
  return "";
}

void DNSManager::dnsUpdate(const String& ip) {
  if (configManager.CF_ZONE == "" || configManager.CF_RECORD == "" || configManager.CF_HOST == "") {
    Serial.println("DNS config missing.");
    return;
  }

  String url = "https://api.cloudflare.com/client/v4/zones/" + configManager.CF_ZONE + "/dns_records/" + configManager.CF_RECORD;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + configManager.CF_TOKEN);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"type\":\"A\",\"name\":\"" + configManager.CF_HOST + "\",\"content\":\"" + ip + "\",\"ttl\":1,\"proxied\":false}";
  int code = http.PATCH(payload);

  if (code > 0) {
    String resp = http.getString();
    if (resp.indexOf("\"success\":true") >= 0) {
      Serial.println("DNS successfully updated!");
    } else {
      Serial.println("Failed to update DNS. Response: " + resp);
    }
  } else {
    Serial.println("Error connecting to Cloudflare. HTTP code: " + String(code));
  }

  http.end();
}

void DNSManager::handleDNSUpdate() {
  String publicIP = getPublicIP();
  if (publicIP == "") return;

  String currentDNSIP = getDNSHostIP(configManager.CF_HOST);
  if (currentDNSIP == "") return;

  if (currentDNSIP != publicIP) {
    Serial.println("Updating DNS...");
    dnsUpdate(publicIP);
  } else {
    Serial.println("DNS is already up-to-date.");
  }
  lastDnsUpdate = millis();
}

bool DNSManager::shouldUpdateDNS() {
  unsigned long now = millis();
  return wifiManager.wifiState == WIFI_OK && 
         (now - lastDnsUpdate >= dnsUpdateInterval || lastDnsUpdate == 0);
}