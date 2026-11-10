/*
 * GPS NEO-6M — Raw Serial Debug Test
 * 
 * This sketch simply reads raw bytes from the GPS
 * and prints them to Serial Monitor.
 * 
 * If you see $GPRMC, $GPGGA sentences → wiring is correct!
 * If you see nothing → wiring problem or dead module.
 * 
 * WIRING:
 *   GPS TX  → ESP32 GPIO 16
 *   GPS RX  → ESP32 GPIO 17
 *   GPS VCC → 3.3V
 *   GPS GND → GND
 */

#define GPS_RX  16
#define GPS_TX  17

HardwareSerial gpsSerial(2);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== GPS Raw Data Debug Test ===");
  Serial.println("If GPS is wired correctly, you should see NMEA sentences below:");
  Serial.println("(lines starting with $GPRMC, $GPGGA, etc.)\n");

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
}

void loop() {
  // Print everything the GPS sends — raw
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    Serial.print(c);
  }
}

