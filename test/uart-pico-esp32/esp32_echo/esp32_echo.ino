// Test UART Pico <-> ESP32 (vedi 02-pico-esp32.md)
// Riceve righe su Serial2 (RX = GPIO16, TX = GPIO17) e le rimanda indietro al Pico.
// Tutto viene anche stampato sulla seriale USB (115200) per il monitor.

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX = 16, TX = 17
  Serial.println("ESP32 echo pronto");
}

void loop() {
  if (Serial2.available()) {
    String riga = Serial2.readStringUntil('\n');
    riga.trim();
    Serial.print("Dal Pico: ");
    Serial.println(riga);
    Serial2.print("ESP32 ha ricevuto: ");
    Serial2.println(riga);
  }
}
