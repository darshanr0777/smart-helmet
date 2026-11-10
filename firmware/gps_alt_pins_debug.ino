/*
 * GPS NEO-6M — Alternative Pins Debug Test
 * 
 * Uses GPIO 25 (RX) and GPIO 26 (TX) instead of 16/17
 * in case your ESP32 board has PSRAM conflict on GPIO 16/17.
 * 
 * WIRING FOR THIS TEST:
 *   GPS TX  → ESP32 GPIO 25
 *   GPS RX  → ESP32 GPIO 26
 *   GPS VCC → 5V (VIN pin)
 *   GPS GND → GND
 */

#define GPS_RX  25    // ← Changed from 16
#define GPS_TX  26    // ← Changed from 17

HardwareSerial gpsSerial(2);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== GPS Debug — Alternative Pins (GPIO 25/26) ===");
  Serial.println("Wiring: GPS TX → GPIO 25 | GPS RX → GPIO 26 | VCC → 5V");
  Serial.println("Looking for NMEA sentences...\n");

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
}

void loop() {
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    Serial.print(c);
  }

  // Print a heartbeat every 5 seconds so you know it's running
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    if (!gpsSerial.available()) {
      Serial.println("[...still waiting for GPS data...]");
    }
  }
}

