/*
 * ==============================================================================
 *  Project : IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 *  Board   : ESP32 Dev Module
 *  Version : 5.0 — Full Combined (DHT11 + MQ-2 + MQ-7 + MQ-135 + GPS NEO-6M)
 *
 *  SENSOR WIRING:
 *    DHT11 Data     → GPIO 4   (Digital, 10kΩ pull-up)
 *    MQ-2 AOUT      → GPIO 34  (Analog — Smoke / LPG)
 *    MQ-7 AOUT      → GPIO 35  (Analog — Carbon Monoxide)
 *    MQ-135 AOUT    → GPIO 32  (Analog — Air Quality / Toxic)
 *    GPS NEO-6M TX  → GPIO 16  (ESP32 RX2 — cross-connected!)
 *    GPS NEO-6M RX  → GPIO 17  (ESP32 TX2 — cross-connected!)
 *    Buzzer (+)     → GPIO 25  (Active Buzzer)
 *    LED (+)        → GPIO 26  (with 220Ω resistor)
 *
 *  LIBRARIES NEEDED (Arduino IDE → Library Manager):
 *    1. DHT sensor library     (by Adafruit)
 *    2. Adafruit Unified Sensor (by Adafruit)
 *    3. TinyGPSPlus            (by Mikal Hart)
 *
 *  SERIAL MONITOR: 115200 baud
 * ==============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <TinyGPSPlus.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// =============================================
// 1. WiFi CREDENTIALS
// =============================================
const char* WIFI_SSID     = "RH-2.4G-99FC50";
const char* WIFI_PASSWORD = "44953B99FC50";

// =============================================
// 2. SUPABASE CONFIGURATION
// =============================================
const char* SUPABASE_URL = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";
const char* WORKER_ID    = "W-101";

// =============================================
// 3. PIN DEFINITIONS
// =============================================
#define PIN_DHT     4
#define DHTTYPE     DHT11

#define PIN_MQ2    34       // MQ-2  — Smoke / LPG / Flammable Gas
#define PIN_MQ7    35       // MQ-7  — Carbon Monoxide (CO)
#define PIN_MQ135  32       // MQ-135 — Air Quality / NH3 / NOx

#define GPS_RX_PIN 16       // ESP32 RX2 ← GPS TX
#define GPS_TX_PIN 17       // ESP32 TX2 → GPS RX
#define GPS_BAUD   9600

#define PIN_BUZZER 25       // Active Buzzer
#define PIN_LED    26       // Alert LED (220Ω resistor)

// =============================================
// 4. GEOFENCE — Mine Center Coordinates
// =============================================
const double MINE_CENTER_LAT    = 12.971598;
const double MINE_CENTER_LNG    = 77.594566;
const double SAFE_RADIUS_METERS = 180.0;

// =============================================
// 5. CALIBRATION & THRESHOLDS
// =============================================
const float MQ2_SCALE    = 3030.0;   // 3.3V full-scale ≈ 10,000 PPM
const float MQ7_SCALE    =   30.0;   // 3.3V full-scale ≈ 100 PPM
const float MQ135_SCALE  =   30.0;   // 3.3V full-scale ≈ 100 PPM

const float TEMP_DANGER_C      = 40.0;    // °C
const float TEMP_WARNING_C     = 35.0;    // °C
const float HUMID_DANGER       = 90.0;    // %RH
const float MQ2_DANGER_PPM     = 10000.0;
const float MQ2_WARNING_PPM    =  5000.0;
const float MQ7_DANGER_PPM     =    50.0;
const float MQ7_WARNING_PPM    =    35.0;
const float MQ135_DANGER_PPM   =    50.0;
const float MQ135_WARNING_PPM  =    25.0;

// =============================================
// 6. TIMING INTERVALS
// =============================================
const unsigned long UPLOAD_INTERVAL_MS  = 3000;   // Upload every 3 seconds
const unsigned long WIFI_CHECK_INTERVAL = 10000;  // WiFi reconnect check every 10s

// =============================================
// 7. OBJECT INSTANCES
// =============================================
DHT dht(PIN_DHT, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);   // UART2

// =============================================
// 8. STATE VARIABLES
// =============================================
unsigned long lastUploadTime     = 0;
unsigned long lastWifiCheckMs    = 0;
unsigned long buzzerPatternStart = 0;
int  uploadCount = 0;
int  failCount   = 0;
bool prevDanger  = false;

// =============================================
// HELPER: Read MQ Gas Sensor (8-sample average)
// =============================================
float readMQ(int pin, float scaleFactor) {
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (sum / 8.0) * (3.3 / 4095.0) * scaleFactor;
}

// =============================================
// HELPER: Haversine Distance (meters)
// =============================================
double calculateDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = (lat2 - lat1) * DEG_TO_RAD;
  double dLon = (lon2 - lon1) * DEG_TO_RAD;
  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
             sin(dLon / 2.0) * sin(dLon / 2.0);
  return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// =============================================
// HELPER: Determine Alert Level
// =============================================
String buildAlertLevel(float temp, float humid, float mq2, float mq7, float mq135, bool geofenceBreach) {
  if (temp >= TEMP_DANGER_C || humid >= HUMID_DANGER ||
      mq2 >= MQ2_DANGER_PPM || mq7 >= MQ7_DANGER_PPM ||
      mq135 >= MQ135_DANGER_PPM || geofenceBreach) {
    return "DANGER";
  }
  if (temp >= TEMP_WARNING_C || mq2 >= MQ2_WARNING_PPM ||
      mq7 >= MQ7_WARNING_PPM || mq135 >= MQ135_WARNING_PPM) {
    return "WARNING";
  }
  return "SAFE";
}

// =============================================
// BUZZER: Rapid Double-Beep Pattern (DANGER)
// =============================================
void runDangerBuzzerPattern() {
  unsigned long phase = (millis() - buzzerPatternStart) % 1500;
  // ON 120ms → OFF 100ms → ON 120ms → OFF 1160ms
  if (phase < 120 || (phase >= 220 && phase < 340)) {
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// =============================================
// BUZZER + LED: Handle Alert Output
// =============================================
void handleBuzzerAndLED(const String& alertLevel) {
  if (alertLevel == "DANGER") {
    digitalWrite(PIN_LED, HIGH);
    runDangerBuzzerPattern();
  } else if (alertLevel == "WARNING") {
    digitalWrite(PIN_LED, HIGH);
    unsigned long phase = (millis() - buzzerPatternStart) % 2000;
    digitalWrite(PIN_BUZZER, (phase < 200) ? HIGH : LOW);
  } else {
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// =============================================
// SUPABASE: Send Telemetry Payload
// =============================================
bool sendToSupabase(float temp, float humid, float mq2, float mq7, float mq135,
                    double lat, double lng, bool gpsFix, int sats, float hdop,
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

  // Supabase required headers
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  // Build JSON telemetry payload
  String body = "{";
  body += "\"worker_id\":\""    + String(WORKER_ID)                      + "\",";
  body += "\"temperature\":"    + String(temp, 2)                        + ",";
  body += "\"humidity\":"       + String(humid, 2)                       + ",";
  body += "\"mq2_smoke\":"      + String(mq2, 2)                        + ",";
  body += "\"mq7_co\":"         + String(mq7, 2)                        + ",";
  body += "\"mq135_air\":"      + String(mq135, 2)                      + ",";
  body += "\"mq3_gas\":0.05,";                                            // Legacy column safe constant
  body += "\"latitude\":"       + String(lat, 6)                         + ",";
  body += "\"longitude\":"      + String(lng, 6)                         + ",";
  body += "\"gps_fix\":"        + String(gpsFix ? "true" : "false")      + ",";
  body += "\"gps_satellites\":" + String(sats)                           + ",";
  body += "\"gps_hdop\":"       + String(hdop, 1)                        + ",";
  body += "\"accel_total\":1.0,";                                         // Placeholder (no MPU in this build)
  body += "\"is_fall\":false,";                                           // Placeholder
  body += "\"inactivity_secs\":0,";                                       // Placeholder
  body += "\"alert_level\":\""  + alertLevel                             + "\"";
  body += "}";

  int httpCode = http.POST(body);

  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] ✅ Success (HTTP %d)\n", httpCode);
    http.end();
    return true;
  }

  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("[Supabase] ❌ FAILED (HTTP %d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("[Supabase] ❌ Network Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return false;
}

// =============================================
// WiFi CONNECT (with Google DNS Fallback)
// =============================================
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);

  // Google Public DNS — prevents hotspot/router DNS failures
  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(8, 8, 4, 4);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] ✅ Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] ❌ Connection failed — will retry in loop.");
  }
}

// =============================================================
// SETUP
// =============================================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // Disable brownout detector
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔═══════════════════════════════════════════════════╗");
  Serial.println("║  SMART HELMET v5.0 — Full Combined Firmware       ║");
  Serial.println("║  DHT11 + MQ-2 + MQ-7 + MQ-135 + GPS NEO-6M       ║");
  Serial.println("║  → Supabase Cloud + Geofencing + Alerts           ║");
  Serial.println("╚═══════════════════════════════════════════════════╝\n");

  // Actuators
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);

  // DHT11
  dht.begin();
  Serial.println("[Init] ✅ DHT11 on GPIO 4");

  // Gas Sensors (analog — no init needed)
  Serial.println("[Init] ✅ MQ-2 on GPIO 34 | MQ-7 on GPIO 35 | MQ-135 on GPIO 32");

  // GPS NEO-6M on UART2
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("[Init] ✅ GPS NEO-6M on RX2=GPIO%d, TX2=GPIO%d @ %d baud\n",
                GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  Serial.println("[Init]    GPS needs clear sky — take outdoors for satellite lock!");

  // Buzzer + LED
  Serial.println("[Init] ✅ Buzzer on GPIO 25 | LED on GPIO 26");

  // WiFi
  connectWiFi();

  // Timers
  buzzerPatternStart = millis();
  lastUploadTime     = millis();
  lastWifiCheckMs    = millis();

  Serial.println("\n[Init] 🚀 Boot complete. Entering monitoring loop...\n");
  Serial.println("═══════════════════════════════════════════════════════");
}

// =============================================================
// MAIN LOOP
// =============================================================
void loop() {

  // ── 1. Feed GPS stream to TinyGPSPlus parser (non-blocking) ──
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // ── 2. WiFi auto-reconnect every 10 seconds ──
  if (millis() - lastWifiCheckMs >= WIFI_CHECK_INTERVAL) {
    lastWifiCheckMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] ⚠️ Disconnected — reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // ── 3. Keep buzzer running between uploads if DANGER ──
  if (prevDanger) {
    runDangerBuzzerPattern();
  }

  // ── 4. Telemetry Read & Upload (every 3 seconds) ──
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL_MS) {
    lastUploadTime = millis();

    // ─── Read DHT11 ───
    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();
    if (isnan(temperature)) temperature = 27.5;  // Fallback if read fails
    if (isnan(humidity))    humidity    = 60.0;

    // ─── Read Gas Sensors ───
    float mq2PPM   = readMQ(PIN_MQ2,   MQ2_SCALE);
    float mq7PPM   = readMQ(PIN_MQ7,   MQ7_SCALE);
    float mq135PPM = readMQ(PIN_MQ135, MQ135_SCALE);

    // ─── Read GPS ───
    double latitude  = MINE_CENTER_LAT;   // Fallback coordinates
    double longitude = MINE_CENTER_LNG;
    bool   gpsFix    = false;
    int    gpsSats   = 0;
    float  gpsHDOP   = 99.9;

    if (gps.location.isValid() && gps.location.age() < 5000) {
      gpsFix    = true;
      latitude  = gps.location.lat();
      longitude = gps.location.lng();
    }
    if (gps.satellites.isValid()) gpsSats = gps.satellites.value();
    if (gps.hdop.isValid())       gpsHDOP = gps.hdop.hdop();

    // ─── Geofence Check ───
    double distFromBase = calculateDistanceMeters(
      latitude, longitude, MINE_CENTER_LAT, MINE_CENTER_LNG
    );
    bool isGeofenceBreached = gpsFix && (distFromBase > SAFE_RADIUS_METERS);

    // ─── Determine Alert Level ───
    String alertLevel = buildAlertLevel(temperature, humidity, mq2PPM, mq7PPM, mq135PPM, isGeofenceBreached);

    // ─── Buzzer + LED ───
    handleBuzzerAndLED(alertLevel);
    prevDanger = (alertLevel == "DANGER");

    // ─── Send to Supabase ───
    bool ok = sendToSupabase(temperature, humidity, mq2PPM, mq7PPM, mq135PPM,
                             latitude, longitude, gpsFix, gpsSats, gpsHDOP,
                             alertLevel);

    if (ok) uploadCount++; else failCount++;

    // ─── Print Summary to Serial Monitor ───
    Serial.println("─────────────────────────────────────────────────────");
    Serial.printf("[Sensors] Temp: %.1f°C | Hum: %.1f%% | MQ-2: %.0f PPM | MQ-7: %.1f PPM | MQ-135: %.1f PPM\n",
                  temperature, humidity, mq2PPM, mq7PPM, mq135PPM);

    // GPS Status
    Serial.printf("[GPS]     Chars: %lu | Sentences: %lu | Fix: %s\n",
                  gps.charsProcessed(), gps.sentencesWithFix(),
                  gpsFix ? "YES ✅" : "NO ⏳");

    if (gpsFix) {
      Serial.printf("[GPS]     Lat: %.6f | Lng: %.6f | Sats: %d | HDOP: %.1f\n",
                    latitude, longitude, gpsSats, gpsHDOP);
      Serial.printf("[GPS]     Distance from mine center: %.1f m | Geofence: %s\n",
                    distFromBase, isGeofenceBreached ? "⚠️ BREACHED!" : "✅ INSIDE");
    } else {
      if (gps.charsProcessed() < 10) {
        Serial.println("[GPS]     ⚠️ NO DATA — Check wiring: GPS TX → ESP32 GPIO 16");
      } else {
        Serial.println("[GPS]     ⏳ Receiving data, waiting for satellite lock (go outdoors)...");
      }
      Serial.println("[GPS]     Using fallback coordinates (mine center).");
    }

    // Overall Status
    Serial.printf("[Status]  Alert: %s | Supabase: %s | Uploads: %d OK / %d Fail\n",
                  alertLevel.c_str(), ok ? "✅" : "❌", uploadCount, failCount);
    Serial.println("─────────────────────────────────────────────────────\n");
  }
}
