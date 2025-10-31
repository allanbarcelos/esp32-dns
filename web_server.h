#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>

class WebServerManager {
private:
  WebServer server{80};

  void handleRoot();
  void handleSaveConfig();

public:
  void setupRoutes();
  void handleClient();
};

extern WebServerManager webServerManager;

#endif