/*
 * ==============================================================
 * GPS NEO-6M + Supabase — Standalone Test Sketch
 * Platform: ESP32 Dev Module
 * 
 * WIRING:
 *   GPS NEO-6M VCC  →  3.3V (or 5V if module has onboard regulator)
 *   GPS NEO-6M GND  →  GND
 *   GPS NEO-6M TX   →  ESP32 GPIO 16 (RX2)
 *   GPS NEO-6M RX   →  ESP32 GPIO 17 (TX2)
 * 
 * LIBRARIES NEEDED (Install via Arduino IDE → Library Manager):
 *   1. TinyGPSPlus (by Mikal Hart)
 * 
 * SERIAL MONITOR: 115200 baud
 * ==============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <TinyGPSPlus.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================================
// 1. WiFi CREDENTIALS — CHANGE THESE!
// ==========================================
const char* WIFI_SSID     = "RH-2.4G-99FC50";
const char* WIFI_PASSWORD = "44953B99FC50";

// ==========================================
// 2. SUPABASE CONFIG — Already set up
// ==========================================
const char* SUPABASE_URL  = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY  = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";
const char* WORKER_ID     = "W-101";

// ==========================================
// 3. GPS PIN DEFINITIONS
// ==========================================
#define GPS_RX_PIN   16       // ESP32 RX2 ← GPS TX
#define GPS_TX_PIN   17       // ESP32 TX2 → GPS RX
#define GPS_BAUD     9600

// ==========================================
// 4. GEOFENCE — Mine Center Coordinates
// ==========================================
const double MINE_CENTER_LAT    = 12.971598;
const double MINE_CENTER_LNG    = 77.594566;
const double SAFE_RADIUS_METERS = 180.0;    // 180m geofence boundary

// ==========================================
// 5. UPLOAD INTERVAL
// ==========================================
const unsigned long UPLOAD_INTERVAL_MS = 5000;  // Send data every 5 seconds
unsigned long lastUploadTime = 0;

// ==========================================
// 6. OBJECTS
// ==========================================
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);  // UART2

// ==========================================
// HELPER: Haversine Distance Calculator
// ==========================================
double calculateDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;  // Earth's radius in meters
  double dLat = (lat2 - lat1) * DEG_TO_RAD;
  double dLon = (lon2 - lon1) * DEG_TO_RAD;
  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
             sin(dLon / 2.0) * sin(dLon / 2.0);
  return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// ==========================================
// WiFi CONNECT (with Google DNS)
// ==========================================
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);

  // Google DNS — prevents hotspot DNS failures
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
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] FAILED — Check SSID & Password!");
  }
}

// ==========================================
// SEND GPS DATA TO SUPABASE
// ==========================================
void sendGPSToSupabase(double lat, double lng, int sats, float hdop, bool gpsFix, String alertLevel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Supabase] Skipped — WiFi not connected.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();           // Skip SSL certificate verification
  client.setTimeout(12);

  HTTPClient http;
  http.begin(client, SUPABASE_URL);
  http.setTimeout(10000);

  // Required Supabase headers
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  // Build JSON payload
  String body = "{";
  body += "\"worker_id\":\"" + String(WORKER_ID)                 + "\",";
  body += "\"latitude\":"    + String(lat, 6)                    + ",";
  body += "\"longitude\":"   + String(lng, 6)                    + ",";
  body += "\"gps_satellites\":" + String(sats)                   + ",";
  body += "\"gps_hdop\":"    + String(hdop, 1)                   + ",";
  body += "\"gps_fix\":"     + String(gpsFix ? "true" : "false") + ",";
  body += "\"temperature\":27.5,";       // Placeholder (no DHT11 in this test)
  body += "\"humidity\":60.0,";          // Placeholder
  body += "\"mq2_smoke\":0.0,";          // Placeholder
  body += "\"mq7_co\":0.0,";            // Placeholder
  body += "\"mq135_air\":0.0,";          // Placeholder
  body += "\"mq3_gas\":0.05,";           // Safe constant
  body += "\"accel_total\":1.0,";        // Placeholder
  body += "\"is_fall\":false,";          // Placeholder
  body += "\"inactivity_secs\":0,";      // Placeholder
  body += "\"alert_level\":\"" + alertLevel + "\"";
  body += "}";

  int httpCode = http.POST(body);

  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] ✅ Success (HTTP %d)\n", httpCode);
  } else if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("[Supabase] ❌ FAILED (HTTP %d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("[Supabase] ❌ Network Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // Disable brownout detector
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║  GPS NEO-6M + Supabase Test Sketch       ║");
  Serial.println("║  Waiting for satellite lock...            ║");
  Serial.println("╚══════════════════════════════════════════╝\n");

  // Initialize GPS on UART2
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("[GPS] Initialized on RX=GPIO%d, TX=GPIO%d @ %d baud\n",
                GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  Serial.println("[GPS] Take the module OUTDOORS or near a window!");
  Serial.println("[GPS] Blinking LED on NEO-6M = satellite lock acquired.\n");

  // Connect WiFi
  connectWiFi();

  lastUploadTime = millis();
  Serial.println("\n[System] Setup complete. Entering GPS monitoring loop...\n");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {

  // 1. Feed incoming GPS serial data to TinyGPSPlus parser
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // 2. Every 5 seconds — read GPS & upload to Supabase
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL_MS) {
    lastUploadTime = millis();

    // Default to mine center (fallback when no fix)
    double currentLat = MINE_CENTER_LAT;
    double currentLng = MINE_CENTER_LNG;
    int    gpsSats    = 0;
    float  gpsHDOP    = 99.9;
    bool   gpsFix     = false;

    // Check if GPS has a valid fix (location updated within last 5 seconds)
    if (gps.location.isValid() && gps.location.age() < 5000) {
      gpsFix     = true;
      currentLat = gps.location.lat();
      currentLng = gps.location.lng();
      gpsSats    = (int)gps.satellites.value();
      gpsHDOP    = (float)gps.hdop.hdop();
    }

    // Geofence check
    double distFromBase = calculateDistanceMeters(
      currentLat, currentLng, MINE_CENTER_LAT, MINE_CENTER_LNG
    );
    bool isGeofenceBreached = gpsFix && (distFromBase > SAFE_RADIUS_METERS);
    String alertLevel = isGeofenceBreached ? "DANGER" : "SAFE";

    // Print GPS status to Serial Monitor
    Serial.println("─────────────────────────────────────────");
    if (gpsFix) {
      Serial.printf("[GPS] ✅ FIX ACQUIRED!\n");
      Serial.printf("[GPS] Latitude  : %.6f\n", currentLat);
      Serial.printf("[GPS] Longitude : %.6f\n", currentLng);
      Serial.printf("[GPS] Satellites: %d\n", gpsSats);
      Serial.printf("[GPS] HDOP      : %.1f (lower = better)\n", gpsHDOP);
      Serial.printf("[GPS] Distance from mine center: %.1f meters\n", distFromBase);
      if (isGeofenceBreached) {
        Serial.println("[ALERT] ⚠️ GEOFENCE BREACHED! Worker outside safe zone!");
      } else {
        Serial.println("[GPS] ✅ Worker inside geofence (SAFE)");
      }
    } else {
      Serial.printf("[GPS] ⏳ Waiting for satellite lock...\n");
      Serial.printf("[GPS] Characters received: %lu\n", gps.charsProcessed());
      Serial.printf("[GPS] Sentences parsed OK: %lu\n", gps.sentencesWithFix());
      if (gps.charsProcessed() < 10) {
        Serial.println("[GPS] ⚠️ NO DATA! Check wiring: GPS TX → ESP32 GPIO 16");
      }
      Serial.println("[GPS] Using fallback coordinates (mine center).");
    }

    // Upload to Supabase
    sendGPSToSupabase(currentLat, currentLng, gpsSats, gpsHDOP, gpsFix, alertLevel);
    Serial.println("─────────────────────────────────────────\n");
  }
}

