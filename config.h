#ifndef CONFIG_H
#define CONFIG_H

#include <Preferences.h>

struct Config {
  char cf_token[64];
  char cf_zone[32];
  char cf_record[32];
  char cf_host[64];
};

class ConfigManager {
private:
  Preferences prefs;
  
public:
  String CF_TOKEN;
  String CF_ZONE;
  String CF_RECORD;
  String CF_HOST;
  
  void loadConfig();
  void saveConfig();
};

extern ConfigManager configManager;

#endif