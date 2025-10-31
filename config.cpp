#include "config.h"

ConfigManager configManager;

void ConfigManager::loadConfig() {
  prefs.begin("myConfig", true);
  CF_TOKEN = prefs.getString("cf_token", "");
  CF_ZONE = prefs.getString("cf_zone", "");
  CF_RECORD = prefs.getString("cf_record", "");
  CF_HOST = prefs.getString("cf_host", "");
  prefs.end();
}

void ConfigManager::saveConfig() {
  prefs.begin("myConfig", false);
  prefs.putString("cf_token", CF_TOKEN);
  prefs.putString("cf_zone", CF_ZONE);
  prefs.putString("cf_record", CF_RECORD);
  prefs.putString("cf_host", CF_HOST);
  prefs.end();
}