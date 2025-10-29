#include <Arduino.h>

unsigned long previousMillis = 0;
const unsigned long interval = 10000; // 10 segundos

void setup() {
  Serial.begin(115200);
  while (!Serial); // Aguarda Serial inicializar (opcional)
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    uint64_t chipid = ESP.getEfuseMac();
    uint32_t chipIdShort = (uint32_t)(chipid >> 32);

    Serial.print("Chip ID: ");
    Serial.println(chipIdShort);
  }

  // Aqui você pode colocar outras tarefas que não travem o loop
}
