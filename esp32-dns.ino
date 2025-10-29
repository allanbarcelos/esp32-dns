#include <WiFi.h>

#define LED_PIN LED_BUILTIN

void setup() {
  // Inicializa serial o mais cedo possível
  Serial.begin(115200);
  while (!Serial) { delay(10); } // garante que a serial esteja pronta
  delay(500);

  Serial.println("=== ESP32 Boot ===");

  pinMode(LED_PIN, OUTPUT);

  // Teste simples de loop para confirmar que o código está rodando
  Serial.println("ESP32 inicializado e pronto para operação.");
}

void loop() {
  // Pisca LED interno para testar loop
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED ON");
  delay(500);

  digitalWrite(LED_PIN, LOW);
  Serial.println("LED OFF");
  delay(500);

  // Mensagem periódica para confirmar que o ESP está vivo
  Serial.println("ESP32 em execução.");
}
