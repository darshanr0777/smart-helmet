/*
 * ============================================================
 *  Project : IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 *  Module  : DHT11 Temperature & Humidity → ESP32 → Supabase
 *  Board   : ESP32 Dev Module (any variant)
 * ============================================================
 *
 *  WIRING DIAGRAM
 *  ──────────────
 *  DHT11 Module      ESP32
 *  ─────────────     ──────────────────
 *  VCC        ──►   3V3  (or 5V if your module has a regulator)
 *  GND        ──►   GND
 *  DATA       ──►   GPIO 4   (PIN_DHT below)
 *
 *  Note: If using a bare DHT11 sensor (not a breakout module),
 *  add a 10 kΩ pull-up resistor between DATA and VCC.
 *
 *  REQUIRED LIBRARIES (install via Arduino Library Manager)
 *  ─────────────────────────────────────────────────────────
 *  1. DHT sensor library  by Adafruit  (install + dependencies)
 *  2. WiFi / HTTPClient   (built-in with ESP32 board package)
 *
 *  BOARD SETUP (Arduino IDE)
 *  ─────────────────────────
 *  Tools → Board     : ESP32 Dev Module
 *  Tools → Port      : (your COM port)
 *  Upload Speed      : 115200
 *
 * ============================================================
 */

// ─────────────────────────────────────────────────────────────
//  INCLUDES
// ─────────────────────────────────────────────────────────────
#include <WiFi.h>               // ESP32 WiFi stack
#include <HTTPClient.h>         // HTTP/HTTPS client
#include <WiFiClientSecure.h>   // TLS/SSL for HTTPS
#include <DHT.h>                // Adafruit DHT sensor library

// ─────────────────────────────────────────────────────────────
//  1. WiFi CREDENTIALS  ← EDIT THESE
// ─────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "darshan";        // Your WiFi / hotspot name
const char* WIFI_PASSWORD = "12345678";       // Your WiFi / hotspot password

// ─────────────────────────────────────────────────────────────
//  2. SUPABASE CONFIGURATION  ← EDIT THESE
//
//  HOW TO FIND YOUR VALUES:
//    Supabase Dashboard → Your Project → Settings → API
//    • Project URL   →  copy the "URL" field
//    • Anon Key      →  copy "anon / public" key
//
//  TABLE ENDPOINT:
//    <ProjectURL>/rest/v1/<table_name>
// ─────────────────────────────────────────────────────────────
const char* SUPABASE_URL = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";

// ─────────────────────────────────────────────────────────────
//  3. WORKER / DEVICE IDENTIFICATION
// ─────────────────────────────────────────────────────────────
const char* WORKER_ID = "W-101";   // Unique ID for this helmet/worker

// ─────────────────────────────────────────────────────────────
//  4. DHT11 PIN & TYPE
// ─────────────────────────────────────────────────────────────
#define PIN_DHT   4        // GPIO 4  (change if needed)
#define DHTTYPE   DHT11    // DHT11 or DHT22

// ─────────────────────────────────────────────────────────────
//  5. SAFETY THRESHOLDS
//     Adjust these to match your mine environment requirements.
// ─────────────────────────────────────────────────────────────
const float TEMP_DANGER_C   = 42.0;   // °C  → triggers DANGER
const float TEMP_WARNING_C  = 35.0;   // °C  → triggers WARNING
const float HUMID_DANGER    = 90.0;   // %   → high humidity is a hazard

// ─────────────────────────────────────────────────────────────
//  6. UPLOAD INTERVAL (milliseconds)
//     3000 ms = push data every 3 seconds
// ─────────────────────────────────────────────────────────────
const unsigned long UPLOAD_INTERVAL_MS = 3000;

// ─────────────────────────────────────────────────────────────
//  7. OBJECT INSTANCES & STATE VARIABLES
// ─────────────────────────────────────────────────────────────
DHT dht(PIN_DHT, DHTTYPE);

unsigned long lastUploadTime  = 0;
unsigned long lastWifiCheckMs = 0;
int           uploadCount     = 0;
int           failCount       = 0;

// ─────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
void   connectWiFi();
bool   ensureWiFi();
String buildAlertLevel(float temp, float humid);
bool   sendToSupabase(float temp, float humid, const String& alertLevel);
void   printDivider();

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  printDivider();
  Serial.println("  Smart Safety Helmet — DHT11 Module");
  Serial.println("  ESP32 → WiFi → Supabase");
  printDivider();

  // Initialise DHT11
  dht.begin();
  Serial.println("[DHT11] Sensor initialized on GPIO " + String(PIN_DHT));
  Serial.println("[DHT11] Warm-up: waiting 2 seconds...");
  delay(2000);   // DHT11 needs >= 1 s after power-up before first reliable read

  // Connect to WiFi
  connectWiFi();

  lastUploadTime = millis();
  Serial.println("\n[System] Setup complete. Starting telemetry loop.");
  printDivider();
}

// ═════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═════════════════════════════════════════════════════════════
void loop() {

  // Re-check WiFi every 10 seconds (auto-reconnect)
  if (millis() - lastWifiCheckMs >= 10000) {
    lastWifiCheckMs = millis();
    ensureWiFi();
  }

  // Upload telemetry at the configured interval
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL_MS) {
    lastUploadTime = millis();

    // Read DHT11
    float temperature = dht.readTemperature();   // Celsius
    float humidity    = dht.readHumidity();      // %RH

    // Validate reading
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("[DHT11] ERROR: Failed to read sensor!");
      Serial.println("        Check wiring: VCC->3V3  GND->GND  DATA->GPIO4");
      failCount++;
      return;  // Skip this cycle; try again next interval
    }

    // Determine alert level
    String alertLevel = buildAlertLevel(temperature, humidity);

    // Print to Serial Monitor
    Serial.printf("\n[DHT11] Temp: %.1f C  |  Humidity: %.1f%%  |  Alert: %s\n",
                  temperature, humidity, alertLevel.c_str());

    // Send to Supabase
    bool ok = sendToSupabase(temperature, humidity, alertLevel);
    if (ok) {
      uploadCount++;
    } else {
      failCount++;
    }
    Serial.printf("[Stats] Uploads OK: %d  |  Fails: %d\n", uploadCount, failCount);
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: connectWiFi
//  Blocking connect at startup (retries up to 30 x 500 ms = 15 s)
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to SSID: \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected!  IP Address: %s\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] RSSI (signal strength): %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("[WiFi] Connection FAILED after 15 seconds.");
    Serial.println("[WiFi] Sensor readings will still be printed locally.");
    Serial.println("[WiFi] Supabase uploads will be skipped until reconnected.");
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: ensureWiFi
//  Non-blocking reconnect check (called every 10 s in loop)
//  Returns true if WiFi is connected.
// ─────────────────────────────────────────────────────────────
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("[WiFi] Disconnected — attempting reconnect...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int i = 0; i < 10; i++) {
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] Reconnected. IP: %s\n",
                    WiFi.localIP().toString().c_str());
      return true;
    }
  }
  Serial.println("[WiFi] Reconnect attempt failed.");
  return false;
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: buildAlertLevel
//  Returns "SAFE", "WARNING", or "DANGER" based on thresholds
// ─────────────────────────────────────────────────────────────
String buildAlertLevel(float temp, float humid) {
  if (temp >= TEMP_DANGER_C || humid >= HUMID_DANGER) {
    return "DANGER";
  } else if (temp >= TEMP_WARNING_C) {
    return "WARNING";
  }
  return "SAFE";
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: sendToSupabase
//  Sends a JSON POST request to the Supabase REST API.
//
//  Supabase REST API rules:
//    URL:     https://<project>.supabase.co/rest/v1/<table>
//    Headers:
//      Content-Type:  application/json
//      apikey:        <anon key>
//      Authorization: Bearer <anon key>
//      Prefer:        return=minimal   <- suppresses response body (saves RAM)
//    Body:    JSON object matching your table columns
//
//  Returns true on HTTP 2xx, false otherwise.
// ─────────────────────────────────────────────────────────────
bool sendToSupabase(float temp, float humid, const String& alertLevel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Supabase] Skipped — WiFi not connected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();   // Skip TLS cert verification (OK for IoT/demo)

  HTTPClient http;
  http.begin(client, SUPABASE_URL);

  // Request Headers
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  // JSON Body — sends only the DHT11 columns.
  // Other columns in the table (mq7_co, gps_fix, etc.) will use
  // their DEFAULT values from the Supabase schema automatically.
  String body = "{";
  body += "\"worker_id\":\""   + String(WORKER_ID) + "\",";
  body += "\"temperature\":"   + String(temp,  2)  + ",";
  body += "\"humidity\":"      + String(humid, 2)  + ",";
  body += "\"alert_level\":\"" + alertLevel        + "\"";
  body += "}";

  Serial.println("[Supabase] Sending POST...");

  int httpCode = http.POST(body);
  String response = http.getString();
  http.end();

  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] Success (HTTP %d)\n", httpCode);
    return true;
  } else {
    Serial.printf("[Supabase] Failed (HTTP %d)\n", httpCode);
    Serial.println("[Supabase] Response: " + response);
    if (httpCode < 0) {
      Serial.println("[Supabase] Hint: Check SUPABASE_URL and internet connectivity.");
    } else if (httpCode == 401) {
      Serial.println("[Supabase] Hint: Check SUPABASE_KEY — Authorization failed.");
    } else if (httpCode == 404) {
      Serial.println("[Supabase] Hint: Check SUPABASE_URL — table name may be wrong.");
    }
    return false;
  }
}

// ─────────────────────────────────────────────────────────────
//  HELPER: printDivider
// ─────────────────────────────────────────────────────────────
void printDivider() {
  Serial.println("─────────────────────────────────────────────");
}
