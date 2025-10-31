#ifndef DNS_MANAGER_H
#define DNS_MANAGER_H

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

class DNSManager {
private:
  const unsigned long dnsUpdateInterval = 300000UL;
  unsigned long lastDnsUpdate = 0;

  String getDNSHostIP(const String& host);
  void dnsUpdate(const String& ip);

public:
  String getPublicIP(); 
  void handleDNSUpdate();
  bool shouldUpdateDNS();
};

extern DNSManager dnsManager;

#endif