#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <LittleFS.h>

class FileManager {
public:
  bool initLittleFS();
  void listFiles();
  void printPartitionUsage();
  bool writeFile(const String& path, const String& content);
  String readFile(const String& path);
};

extern FileManager fileManager;

#endif