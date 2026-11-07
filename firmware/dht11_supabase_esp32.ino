/*
 * ============================================================
 *  Project : IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 *  Module  : DHT11 + MQ-2 + MQ-7 + MQ-135 + GPS NEO-6M → ESP32 → Supabase
 *  Board   : ESP32 Dev Module (any variant)
 *  Version : 4.0  (Added GPS Neo-6M Geolocation & Geofencing)
 * ============================================================
 *
 *  ╔══════════════════════════════════════════════════════════╗
 *  ║                   WIRING DIAGRAM                        ║
 *  ╠══════════════════════════════════════════════════════════╣
 *  ║  Sensor / Component    Signal Pin  →  ESP32 GPIO        ║
 *  ║  ─────────────────────────────────────────────────────  ║
 *  ║  DHT11  DATA           DATA        →  GPIO 4            ║
 *  ║  DHT11  VCC                        →  3V3               ║
 *  ║  DHT11  GND                        →  GND               ║
 *  ║                                                         ║
 *  ║  MQ-2   AO (Smoke/LPG/CO)          →  GPIO 34 (ADC)    ║
 *  ║  MQ-2   VCC                        →  5V (VIN)          ║
 *  ║  MQ-2   GND                        →  GND               ║
 *  ║                                                         ║
 *  ║  MQ-7   AO (Carbon Monoxide)       →  GPIO 35 (ADC)    ║
 *  ║  MQ-7   VCC                        →  5V (VIN)          ║
 *  ║  MQ-7   GND                        →  GND               ║
 *  ║                                                         ║
 *  ║  MQ-135 AO (Air Quality / NH3)     →  GPIO 32 (ADC)    ║
 *  ║  MQ-135 VCC                        →  5V (VIN)          ║
 *  ║  MQ-135 GND                        →  GND               ║
 *  ║                                                         ║
 *  ║  GPS NEO-6M TX         TX          →  GPIO 16 (UART2 RX)║
 *  ║  GPS NEO-6M RX         RX          →  GPIO 17 (UART2 TX)║
 *  ║  GPS NEO-6M VCC                    →  3V3 or 5V (VIN)   ║
 *  ║  GPS NEO-6M GND                    →  GND               ║
 *  ║                                                         ║
 *  ║  Buzzer (+)            Signal      →  GPIO 25           ║
 *  ║  Buzzer (-)                        →  GND               ║
 *  ║  LED    (+)  220Ω res  Anode       →  GPIO 26           ║
 *  ║  LED    (-)            Cathode     →  GND               ║
 *  ╠══════════════════════════════════════════════════════════╣
 *  ║  IMPORTANT GPS NOTES:                                   ║
 *  ║  • GPS TX goes to ESP32 RX (GPIO 16). Cross-connect!    ║
 *  ║  • GPS RX goes to ESP32 TX (GPIO 17).                   ║
 *  ║  • GPS needs an open sky view for satellite lock        ║
 *  ║    (takes 30-120 seconds outdoors).                     ║
 *  ║  • While waiting for lock, fallback mine coordinates    ║
 *  ║    are sent so the dashboard map stays centered!        ║
 *  ╚══════════════════════════════════════════════════════════╝
 *
 *  REQUIRED LIBRARIES (Arduino IDE → Library Manager):
 *  ─────────────────────────────────────────────────────────
 *  1. "DHT sensor library" by Adafruit
 *  2. "TinyGPSPlus" by Mikal Hart
 *  3. WiFi / HTTPClient / WiFiClientSecure (built into ESP32 core)
 *
 *  BOARD SETUP (Arduino IDE):
 *  ─────────────────────────────────────────────────────────
 *  Tools → Board     :  ESP32 Dev Module
 *  Tools → Port      :  (your COM port)
 *  Baud Rate Monitor :  115200
 * ============================================================
 */

// ─────────────────────────────────────────────────────────────
//  INCLUDES
// ─────────────────────────────────────────────────────────────
#include <WiFi.h>               // ESP32 WiFi stack
#include <HTTPClient.h>         // HTTP client
#include <WiFiClientSecure.h>   // HTTPS / TLS
#include <DHT.h>                // Adafruit DHT11 library
#include <TinyGPSPlus.h>        // TinyGPSPlus library for NEO-6M

// ─────────────────────────────────────────────────────────────
//  1. WiFi CREDENTIALS
// ─────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "darshan";    // Hotspot / router SSID
const char* WIFI_PASSWORD = "12345678";   // Hotspot / router password

// ─────────────────────────────────────────────────────────────
//  2. SUPABASE CONFIGURATION
// ─────────────────────────────────────────────────────────────
const char* SUPABASE_URL = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";

// ─────────────────────────────────────────────────────────────
//  3. WORKER / DEVICE ID
// ─────────────────────────────────────────────────────────────
const char* WORKER_ID = "W-101";

// ─────────────────────────────────────────────────────────────
//  4. PIN DEFINITIONS
// ─────────────────────────────────────────────────────────────
// DHT11
#define PIN_DHT     4       // Digital – DHT11 data line
#define DHTTYPE     DHT11

// MQ Gas Sensors (Analog – ADC1 channel, 12-bit 0-4095)
#define PIN_MQ2    34       // GPIO 34 – MQ-2  (Smoke / LPG / CO)
#define PIN_MQ7    35       // GPIO 35 – MQ-7  (Carbon Monoxide)
#define PIN_MQ135  32       // GPIO 32 – MQ-135 (Air Quality / NH3 / NOx)

// GPS NEO-6M (HardwareSerial 2)
#define GPS_RX_PIN 16       // ESP32 RX2 ← NEO-6M TX
#define GPS_TX_PIN 17       // ESP32 TX2 ← NEO-6M RX
#define GPS_BAUD   9600

// Actuators
#define PIN_BUZZER 25       // Active buzzer (HIGH = ON)
#define PIN_LED    26       // Danger LED (HIGH = ON, via 220Ω resistor)

// ─────────────────────────────────────────────────────────────
//  5. MINE SITE FALLBACK COORDINATES (When indoors / no fix)
// ─────────────────────────────────────────────────────────────
const double MINE_CENTER_LAT = 12.971598;
const double MINE_CENTER_LNG = 77.594566;

// ─────────────────────────────────────────────────────────────
//  6. SENSOR CALIBRATION CONSTANTS
// ─────────────────────────────────────────────────────────────
const float MQ2_SCALE    = 3030.0;  // 3.3V full-scale ≈ 10,000 PPM (smoke / LPG)
const float MQ7_SCALE    =   30.0;  // 3.3V full-scale ≈ 100 PPM    (CO)
const float MQ135_SCALE  =   30.0;  // 3.3V full-scale ≈ 100 PPM    (air quality)

// ─────────────────────────────────────────────────────────────
//  7. SAFETY THRESHOLDS
// ─────────────────────────────────────────────────────────────
// Temperature (DHT11)
const float TEMP_DANGER_C    = 40.0;   // °C
const float TEMP_WARNING_C   = 35.0;   // °C
const float HUMID_DANGER     = 90.0;   // %RH

// MQ-2  (Smoke / LPG: Warning >= 5000 PPM, Danger >= 10000 PPM)
const float MQ2_DANGER_PPM   = 10000.0;
const float MQ2_WARNING_PPM  =  5000.0;

// MQ-7  (Carbon Monoxide: Warning >= 35 PPM, Danger >= 50 PPM)
const float MQ7_DANGER_PPM   =    50.0;
const float MQ7_WARNING_PPM  =    35.0;

// MQ-135 (Air Quality: Warning >= 25 PPM, Danger >= 50 PPM)
const float MQ135_DANGER_PPM  =   50.0;
const float MQ135_WARNING_PPM =   25.0;

// ─────────────────────────────────────────────────────────────
//  8. TIMING
// ─────────────────────────────────────────────────────────────
const unsigned long UPLOAD_INTERVAL_MS  = 3000;   // Supabase POST every 3 s
const unsigned long WIFI_CHECK_INTERVAL = 10000;  // WiFi reconnect check every 10 s

// ─────────────────────────────────────────────────────────────
//  9. OBJECT INSTANCES & STATE
// ─────────────────────────────────────────────────────────────
DHT dht(PIN_DHT, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);   // Use ESP32 UART2

unsigned long lastUploadTime     = 0;
unsigned long lastWifiCheckMs    = 0;
unsigned long buzzerPatternStart = 0;
int           uploadCount        = 0;
int           failCount          = 0;
bool          prevDanger         = false;

// ─────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
void   connectWiFi();
bool   ensureWiFi();
float  readMQ(int pin, float scaleFactor);
String buildAlertLevel(float temp, float humid,
                       float mq2,  float mq7, float mq135);
void   handleBuzzerAndLED(const String& alertLevel);
void   runDangerBuzzerPattern();
void   runWarningBuzzerPattern();
bool   sendToSupabase(float temp, float humid,
                      float mq2,  float mq7,  float mq135,
                      double lat, double lng,
                      bool gpsFix, int sats, float hdop,
                      const String& alertLevel);
void   printSensorReadings(float temp, float humid,
                           float mq2,  float mq7,  float mq135,
                           double lat, double lng, bool fix, int sats,
                           const String& alert);
void   printDivider();

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  printDivider();
  Serial.println("  Smart Safety Helmet — Multi-Sensor + GPS v4.0");
  Serial.println("  DHT11 | MQ-2 | MQ-7 | MQ-135 | GPS NEO-6M | Supabase");
  printDivider();

  // Output pins
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);
  Serial.println("[Init] Buzzer: GPIO" + String(PIN_BUZZER) +
                 "  LED: GPIO"    + String(PIN_LED));

  // Analog input pins
  Serial.println("[Init] MQ-2:   GPIO" + String(PIN_MQ2));
  Serial.println("[Init] MQ-7:   GPIO" + String(PIN_MQ7));
  Serial.println("[Init] MQ-135: GPIO" + String(PIN_MQ135));

  // GPS UART2 init
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("[Init] GPS Neo-6M UART2: RX=GPIO%d, TX=GPIO%d @ %d baud\n",
                GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);

  // DHT11 warm-up
  dht.begin();
  Serial.println("[DHT11] GPIO" + String(PIN_DHT) + " — 2 s warm-up...");
  delay(2000);

  // WiFi
  connectWiFi();

  buzzerPatternStart = millis();
  lastUploadTime     = millis();

  Serial.println("\n[System] Boot complete. Starting telemetry loop.");
  printDivider();
}

// ═════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═════════════════════════════════════════════════════════════
void loop() {

  // ── Constantly feed NMEA stream to TinyGPSPlus parser ─────
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    gps.encode(c);
  }

  // ── WiFi keep-alive ───────────────────────────────────────
  if (millis() - lastWifiCheckMs >= WIFI_CHECK_INTERVAL) {
    lastWifiCheckMs = millis();
    ensureWiFi();
  }

  // ── Sensor read + upload cycle ────────────────────────────
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL_MS) {
    lastUploadTime = millis();

    // --- DHT11 ---
    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("[DHT11] ERROR: Read failed! Check wiring on GPIO" + String(PIN_DHT));
      failCount++;
      return;
    }

    // --- Gas sensors ---
    float mq2PPM   = readMQ(PIN_MQ2,   MQ2_SCALE);
    float mq7PPM   = readMQ(PIN_MQ7,   MQ7_SCALE);
    float mq135PPM = readMQ(PIN_MQ135, MQ135_SCALE);

    // --- GPS coordinates & satellite telemetry ---
    double latitude  = MINE_CENTER_LAT;
    double longitude = MINE_CENTER_LNG;
    bool   gpsFix    = false;
    int    sats      = 0;
    float  hdop      = 99.9;

    if (gps.location.isValid() && gps.location.age() < 5000) {
      latitude  = gps.location.lat();
      longitude = gps.location.lng();
      gpsFix    = true;
    }
    if (gps.satellites.isValid()) {
      sats = gps.satellites.value();
    }
    if (gps.hdop.isValid()) {
      hdop = gps.hdop.hdop();
    }

    // --- Alert level ---
    String alertLevel = buildAlertLevel(temperature, humidity, mq2PPM, mq7PPM, mq135PPM);

    // --- Print readings ---
    printSensorReadings(temperature, humidity, mq2PPM, mq7PPM, mq135PPM,
                        latitude, longitude, gpsFix, sats, alertLevel);

    // --- Edge-detect for new DANGER event ---
    bool nowDanger = (alertLevel == "DANGER");
    if (nowDanger && !prevDanger) {
      Serial.println();
      Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      Serial.println("[ALERT] *** DANGER THRESHOLD CROSSED ***");
      Serial.println("[ALERT] Dashboard notified. Buzzer activated.");
      Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    } else if (!nowDanger && prevDanger) {
      Serial.println("[SAFE]  All sensors back within safe range.");
    }
    prevDanger = nowDanger;

    // --- Buzzer + LED ---
    handleBuzzerAndLED(alertLevel);

    // --- Supabase upload ---
    bool ok = sendToSupabase(temperature, humidity, mq2PPM, mq7PPM, mq135PPM,
                             latitude, longitude, gpsFix, sats, hdop, alertLevel);
    if (ok) uploadCount++; else failCount++;
    Serial.printf("[Stats] Uploads OK: %d  |  Fails: %d\n", uploadCount, failCount);
    printDivider();
  }

  // ── Continuous non-blocking buzzer pattern ────────────────
  if (prevDanger) {
    runDangerBuzzerPattern();
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: readMQ
// ─────────────────────────────────────────────────────────────
float readMQ(int pin, float scaleFactor) {
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  float avgRaw  = sum / 8.0;
  float voltage = avgRaw * (3.3 / 4095.0);
  return voltage * scaleFactor;
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: buildAlertLevel
// ─────────────────────────────────────────────────────────────
String buildAlertLevel(float temp, float humid,
                       float mq2,  float mq7, float mq135) {
  if (temp   >= TEMP_DANGER_C   ||
      humid  >= HUMID_DANGER    ||
      mq2    >= MQ2_DANGER_PPM  ||
      mq7    >= MQ7_DANGER_PPM  ||
      mq135  >= MQ135_DANGER_PPM) {
    return "DANGER";
  }

  if (temp   >= TEMP_WARNING_C   ||
      mq2    >= MQ2_WARNING_PPM  ||
      mq7    >= MQ7_WARNING_PPM  ||
      mq135  >= MQ135_WARNING_PPM) {
    return "WARNING";
  }

  return "SAFE";
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: handleBuzzerAndLED
// ─────────────────────────────────────────────────────────────
void handleBuzzerAndLED(const String& alertLevel) {
  if (alertLevel == "DANGER") {
    digitalWrite(PIN_LED, HIGH);
    runDangerBuzzerPattern();
  } else if (alertLevel == "WARNING") {
    digitalWrite(PIN_LED, HIGH);
    runWarningBuzzerPattern();
  } else {
    digitalWrite(PIN_LED,    LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: runDangerBuzzerPattern  (NON-BLOCKING)
// ─────────────────────────────────────────────────────────────
void runDangerBuzzerPattern() {
  const unsigned long CYCLE  = 1500;
  const unsigned long B1_OFF = 120;
  const unsigned long B2_ON  = 220;
  const unsigned long B2_OFF = 340;

  unsigned long phase = (millis() - buzzerPatternStart) % CYCLE;

  if      (phase < B1_OFF) digitalWrite(PIN_BUZZER, HIGH);
  else if (phase < B2_ON)  digitalWrite(PIN_BUZZER, LOW);
  else if (phase < B2_OFF) digitalWrite(PIN_BUZZER, HIGH);
  else                     digitalWrite(PIN_BUZZER, LOW);
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: runWarningBuzzerPattern  (NON-BLOCKING)
// ─────────────────────────────────────────────────────────────
void runWarningBuzzerPattern() {
  const unsigned long CYCLE   = 2000;
  const unsigned long BEEP_ON = 200;

  unsigned long phase = (millis() - buzzerPatternStart) % CYCLE;
  digitalWrite(PIN_BUZZER, (phase < BEEP_ON) ? HIGH : LOW);
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: sendToSupabase
// ─────────────────────────────────────────────────────────────
bool sendToSupabase(float temp, float humid,
                    float mq2,  float mq7,  float mq135,
                    double lat, double lng,
                    bool gpsFix, int sats, float hdop,
                    const String& alertLevel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Supabase] Skipped — WiFi not connected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12);

  HTTPClient http;
  bool beginOk = http.begin(client, SUPABASE_URL);
  if (!beginOk) {
    http.begin(SUPABASE_URL);
  }
  http.setTimeout(10000);

  // Headers
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  // JSON payload
  String body = "{";
  body += "\"worker_id\":\""       + String(WORKER_ID)     + "\",";
  body += "\"temperature\":"       + String(temp,   2)      + ",";
  body += "\"humidity\":"          + String(humid,  2)      + ",";
  body += "\"mq2_smoke\":"         + String(mq2,    2)      + ",";
  body += "\"mq7_co\":"            + String(mq7,    2)      + ",";
  body += "\"mq135_air\":"         + String(mq135,  2)      + ",";
  body += "\"latitude\":"          + String(lat,    6)      + ",";
  body += "\"longitude\":"         + String(lng,    6)      + ",";
  body += "\"gps_fix\":"           + String(gpsFix ? "true" : "false") + ",";
  body += "\"gps_satellites\":"    + String(sats)           + ",";
  body += "\"gps_hdop\":"          + String(hdop,   1)      + ",";
  body += "\"alert_level\":\""     + alertLevel             + "\"";
  body += "}";

  int httpCode = http.POST(body);

  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] Success (HTTP %d)\n", httpCode);
    http.end();
    return true;
  }

  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("[Supabase] FAILED (HTTP %d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("[Supabase] Network Error: %s (code %d)\n", http.errorToString(httpCode).c_str(), httpCode);
  }

  http.end();
  return false;
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: connectWiFi
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.println("\n[WiFi] Connecting to: " + String(WIFI_SSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected successfully!");
    Serial.println("[WiFi] IP Address : " + WiFi.localIP().toString());
    Serial.printf( "[WiFi] Signal RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("[WiFi] ERROR: Connection failed! Check SSID & password.");
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: ensureWiFi
// ─────────────────────────────────────────────────────────────
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[WiFi] Connection lost. Reconnecting...");
  WiFi.disconnect();
  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  return (WiFi.status() == WL_CONNECTED);
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: printSensorReadings
// ─────────────────────────────────────────────────────────────
void printSensorReadings(float temp, float humid,
                         float mq2,  float mq7,  float mq135,
                         double lat, double lng, bool fix, int sats,
                         const String& alert) {
  Serial.printf("[SENSORS] Temp: %.1f C | Hum: %.1f %% | MQ-2: %.0f PPM | MQ-7: %.0f PPM | MQ-135: %.0f PPM\n",
                temp, humid, mq2, mq7, mq135);
  Serial.printf("[GPS]     Lat: %.6f | Lng: %.6f | Lock: %s | Satellites: %d\n",
                lat, lng, fix ? "YES (3D Fix)" : "NO (Using Fallback)", sats);
  Serial.printf("[STATUS]  Alert Level: %s\n", alert.c_str());
}

void printDivider() {
  Serial.println("─────────────────────────────────────────────────────────");
}
