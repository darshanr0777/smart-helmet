/*
 * ============================================================
 *  Project : IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 *  Module  : DHT11 Temperature & Humidity → ESP32 → Supabase
 *  Board   : ESP32 Dev Module (any variant)
 *  Version : 2.0  (Updated: Temperature alert at 40°C with
 *                   buzzer pattern + Supabase DANGER payload)
 * ============================================================
 *
 *  WIRING DIAGRAM
 *  ─────────────────────────────────────────────────────────
 *  Component         Pin        ESP32 GPIO
 *  ─────────────     ─────────  ──────────
 *  DHT11 VCC    ──►  3V3        (3.3V rail)
 *  DHT11 GND    ──►  GND
 *  DHT11 DATA   ──►  GPIO 4     (PIN_DHT)
 *  Buzzer  (+)  ──►  GPIO 25    (PIN_BUZZER) ← active-HIGH
 *  Buzzer  (-)  ──►  GND
 *  LED     (+)  ──►  GPIO 26    (PIN_LED)   ← through 220Ω
 *  LED     (-)  ──►  GND
 *
 *  Note: If using a bare DHT11 (no breakout board), add a
 *  10 kΩ pull-up resistor between DATA and VCC.
 *
 *  REQUIRED LIBRARIES  (Arduino IDE → Library Manager)
 *  ─────────────────────────────────────────────────────────
 *  1. "DHT sensor library"  by Adafruit  + its dependencies
 *  2. WiFi / HTTPClient / WiFiClientSecure — built-in with
 *     the ESP32 Arduino board package (no extra install needed)
 *
 *  BOARD SETUP
 *  ─────────────────────────────────────────────────────────
 *  Tools → Board     :  ESP32 Dev Module
 *  Tools → Port      :  (select your COM port)
 *  Upload Speed      :  115200
 *
 *  HOW IT WORKS
 *  ─────────────────────────────────────────────────────────
 *  Every 3 seconds the ESP32:
 *   1. Reads temperature & humidity from the DHT11.
 *   2. Evaluates alert level: SAFE / WARNING / DANGER.
 *   3. If DANGER (temp ≥ 40°C or humidity ≥ 90%):
 *        • Activates LED solid ON.
 *        • Runs a rapid double-beep buzzer pattern (heat-specific).
 *        • Sends alert_level = "DANGER" to Supabase.
 *      If WARNING (temp ≥ 35°C):
 *        • Activates LED solid ON.
 *        • Runs a slow single-beep buzzer pattern.
 *        • Sends alert_level = "WARNING" to Supabase.
 *      If SAFE:
 *        • LED and buzzer OFF.
 *        • Sends alert_level = "SAFE" to Supabase.
 *   4. Dashboard receives the insert via Supabase Realtime
 *      and plays a distinct audio tone for the heat alert.
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
const char* WIFI_SSID     = "darshan";    // Your WiFi / hotspot SSID
const char* WIFI_PASSWORD = "12345678";   // Your WiFi / hotspot password

// ─────────────────────────────────────────────────────────────
//  2. SUPABASE CONFIGURATION  ← EDIT THESE
//
//  HOW TO FIND YOUR VALUES:
//    Supabase Dashboard → Project → Settings → API
//      "Project URL"       → paste into SUPABASE_URL below
//      "anon / public" key → paste into SUPABASE_KEY below
//
//  REST endpoint format:
//    https://<project-ref>.supabase.co/rest/v1/<table-name>
// ─────────────────────────────────────────────────────────────
const char* SUPABASE_URL = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";

// ─────────────────────────────────────────────────────────────
//  3. WORKER / DEVICE IDENTIFICATION
// ─────────────────────────────────────────────────────────────
const char* WORKER_ID = "W-101";   // Unique ID for this helmet / worker

// ─────────────────────────────────────────────────────────────
//  4. PIN DEFINITIONS
// ─────────────────────────────────────────────────────────────
#define PIN_DHT     4    // DHT11 data line
#define PIN_BUZZER  25   // Active buzzer (or passive buzzer driven HIGH)
#define PIN_LED     26   // Danger indicator LED (via 220Ω resistor to GND)
#define DHTTYPE     DHT11

// ─────────────────────────────────────────────────────────────
//  5. SAFETY THRESHOLDS
//     All values match the main smart_helmet_esp32.ino firmware.
// ─────────────────────────────────────────────────────────────
const float TEMP_DANGER_C   = 40.0;  // °C → DANGER alert (buzzer double-beep)
const float TEMP_WARNING_C  = 35.0;  // °C → WARNING alert (buzzer slow beep)
const float HUMID_DANGER    = 90.0;  // %  → DANGER alert

// ─────────────────────────────────────────────────────────────
//  6. TIMING CONFIGURATION
// ─────────────────────────────────────────────────────────────
const unsigned long UPLOAD_INTERVAL_MS  = 3000;   // Send to Supabase every 3 s
const unsigned long WIFI_CHECK_INTERVAL = 10000;  // Check WiFi every 10 s

// ─────────────────────────────────────────────────────────────
//  7. OBJECT INSTANCES & STATE
// ─────────────────────────────────────────────────────────────
DHT dht(PIN_DHT, DHTTYPE);

unsigned long lastUploadTime    = 0;
unsigned long lastWifiCheckMs   = 0;
unsigned long buzzerPatternStart= 0;  // Reference point for non-blocking buzzer
int           uploadCount       = 0;
int           failCount         = 0;
bool          prevTempDanger    = false;  // Edge-detect: first time crossing 40°C

// ─────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
void   connectWiFi();
bool   ensureWiFi();
String buildAlertLevel(float temp, float humid);
void   handleBuzzerAndLED(const String& alertLevel);
void   runDangerBuzzerPattern();
void   runWarningBuzzerPattern();
bool   sendToSupabase(float temp, float humid, const String& alertLevel);
void   printDivider();

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  printDivider();
  Serial.println("  Smart Safety Helmet — DHT11 + Supabase v2.0");
  Serial.println("  ESP32  →  DHT11  →  WiFi  →  Supabase Cloud");
  printDivider();

  // Configure output pins
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);
  Serial.println("[Init] Buzzer GPIO " + String(PIN_BUZZER) + "  LED GPIO " + String(PIN_LED));

  // DHT11 warm-up
  dht.begin();
  Serial.println("[DHT11] Sensor on GPIO " + String(PIN_DHT) + " — waiting 2 s warm-up...");
  delay(2000);   // DHT11 needs ≥1 s before first reliable reading

  // WiFi connect
  connectWiFi();

  buzzerPatternStart = millis();
  lastUploadTime     = millis();

  Serial.println("\n[System] Boot complete. Entering telemetry loop.");
  printDivider();
}

// ═════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═════════════════════════════════════════════════════════════
void loop() {

  // ── Auto WiFi reconnect every 10 s ───────────────────────
  if (millis() - lastWifiCheckMs >= WIFI_CHECK_INTERVAL) {
    lastWifiCheckMs = millis();
    ensureWiFi();
  }

  // ── Read & upload every 3 s ───────────────────────────────
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL_MS) {
    lastUploadTime = millis();

    // --- Read DHT11 ---
    float temperature = dht.readTemperature();  // °C
    float humidity    = dht.readHumidity();     // %RH

    // Validate
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("[DHT11] ERROR: Sensor read failed!");
      Serial.println("        Wiring check: VCC→3V3  GND→GND  DATA→GPIO" + String(PIN_DHT));
      failCount++;
      return;
    }

    // --- Alert level ---
    String alertLevel = buildAlertLevel(temperature, humidity);

    // --- Serial output ---
    Serial.println();
    printDivider();
    Serial.printf("[DHT11]    Temperature : %.1f C\n", temperature);
    Serial.printf("[DHT11]    Humidity    : %.1f %%\n", humidity);
    Serial.printf("[DHT11]    Alert Level : %s\n", alertLevel.c_str());

    // --- Edge-detect: first crossing into DANGER ---
    bool nowDanger = (alertLevel == "DANGER");
    if (nowDanger && !prevTempDanger) {
      Serial.println();
      Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      Serial.printf("[ALERT] TEMPERATURE DANGER: %.1f C >= %.1f C !\n",
                    temperature, TEMP_DANGER_C);
      Serial.println("[ALERT] Dashboard notified. Buzzer activated.");
      Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    } else if (!nowDanger && prevTempDanger) {
      Serial.println("[SAFE] Temperature back within safe range.");
    }
    prevTempDanger = nowDanger;

    // --- Buzzer + LED ---
    handleBuzzerAndLED(alertLevel);

    // --- Supabase upload ---
    bool ok = sendToSupabase(temperature, humidity, alertLevel);
    if (ok) uploadCount++; else failCount++;
    Serial.printf("[Stats] OK: %d  Fail: %d\n", uploadCount, failCount);
    printDivider();
  }

  // ── Run buzzer pattern continuously (non-blocking) ────────
  // This keeps the buzzer pattern running between upload cycles.
  // We re-read alertLevel from DHT would be expensive here,
  // so we rely on prevTempDanger flag set in the upload block.
  // (The pattern will continue until the next upload cycle re-evaluates.)
  if (prevTempDanger) {
    runDangerBuzzerPattern();
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: connectWiFi
//  Blocking startup connect — waits up to 15 seconds.
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to \"%s\"", WIFI_SSID);
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
    Serial.printf("[WiFi] Connected!  IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFi] FAILED — running in offline mode.");
    Serial.println("[WiFi] Readings printed locally; uploads skipped.");
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: ensureWiFi
//  Non-blocking reconnect check. Returns true if connected.
// ─────────────────────────────────────────────────────────────
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("[WiFi] Disconnected — reconnecting...");
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
  Serial.println("[WiFi] Reconnect failed.");
  return false;
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: buildAlertLevel
//  Returns "SAFE", "WARNING", or "DANGER"
// ─────────────────────────────────────────────────────────────
String buildAlertLevel(float temp, float humid) {
  if (temp >= TEMP_DANGER_C || humid >= HUMID_DANGER) return "DANGER";
  if (temp >= TEMP_WARNING_C)                         return "WARNING";
  return "SAFE";
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: handleBuzzerAndLED
//  Dispatches correct buzzer/LED state based on alert level.
//
//  DANGER  → LED ON solid + rapid double-beep pattern
//  WARNING → LED ON solid + slow single-beep pattern
//  SAFE    → LED OFF + buzzer OFF
// ─────────────────────────────────────────────────────────────
void handleBuzzerAndLED(const String& alertLevel) {
  if (alertLevel == "DANGER") {
    digitalWrite(PIN_LED, HIGH);
    runDangerBuzzerPattern();       // Rapid double-beep
  } else if (alertLevel == "WARNING") {
    digitalWrite(PIN_LED, HIGH);
    runWarningBuzzerPattern();      // Slow single beep
  } else {
    digitalWrite(PIN_LED,    LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: runDangerBuzzerPattern  (NON-BLOCKING)
//
//  Rapid double-beep every 1.5 seconds:
//    Beep-1 ON  120 ms
//    Gap        100 ms
//    Beep-2 ON  120 ms
//    Silence   1160 ms
//    ─────────────────
//    Total    1500 ms  (repeats continuously)
//
//  Uses millis() — never blocks the main loop.
// ─────────────────────────────────────────────────────────────
void runDangerBuzzerPattern() {
  const unsigned long CYCLE   = 1500;
  const unsigned long B1_ON   = 0;
  const unsigned long B1_OFF  = 120;
  const unsigned long B2_ON   = 220;
  const unsigned long B2_OFF  = 340;

  unsigned long phase = (millis() - buzzerPatternStart) % CYCLE;

  if      (phase < B1_OFF) digitalWrite(PIN_BUZZER, HIGH);  // Beep 1
  else if (phase < B2_ON)  digitalWrite(PIN_BUZZER, LOW);   // Gap
  else if (phase < B2_OFF) digitalWrite(PIN_BUZZER, HIGH);  // Beep 2
  else                     digitalWrite(PIN_BUZZER, LOW);   // Silence
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: runWarningBuzzerPattern  (NON-BLOCKING)
//
//  Slow single-beep every 2 seconds:
//    Beep ON   200 ms
//    Silence  1800 ms
// ─────────────────────────────────────────────────────────────
void runWarningBuzzerPattern() {
  const unsigned long CYCLE   = 2000;
  const unsigned long BEEP_ON = 200;

  unsigned long phase = (millis() - buzzerPatternStart) % CYCLE;
  digitalWrite(PIN_BUZZER, (phase < BEEP_ON) ? HIGH : LOW);
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: sendToSupabase
//
//  Sends an HTTPS POST to Supabase REST API.
//
//  Required Headers:
//    Content-Type:  application/json
//    apikey:        <your anon key>
//    Authorization: Bearer <your anon key>
//    Prefer:        return=minimal   ← skip response body (saves RAM)
//
//  JSON Payload Columns sent:
//    worker_id   → VARCHAR  identifies this helmet
//    temperature → NUMERIC  °C from DHT11
//    humidity    → NUMERIC  %RH from DHT11
//    alert_level → VARCHAR  "SAFE" / "WARNING" / "DANGER"
//
//  All other columns in helmet_telemetry (mq7_co, gps_fix, etc.)
//  automatically use their DEFAULT values from the schema.
//
//  Returns true on HTTP 2xx, false on any error.
// ─────────────────────────────────────────────────────────────
bool sendToSupabase(float temp, float humid, const String& alertLevel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Supabase] Skipped — no WiFi.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();   // Skip TLS certificate check (acceptable for IoT)

  HTTPClient http;
  http.begin(client, SUPABASE_URL);

  // ── Headers ───────────────────────────────────────────────
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  // ── JSON body ─────────────────────────────────────────────
  String body = "{";
  body += "\"worker_id\":\""   + String(WORKER_ID) + "\",";
  body += "\"temperature\":"   + String(temp,  2)  + ",";
  body += "\"humidity\":"      + String(humid, 2)  + ",";
  body += "\"alert_level\":\"" + alertLevel        + "\"";
  body += "}";

  Serial.println("[Supabase] POST → " + alertLevel + " payload...");

  int    httpCode = http.POST(body);
  String response = http.getString();
  http.end();

  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] Success (HTTP %d)\n", httpCode);
    return true;
  }

  // Error handling with specific hints
  Serial.printf("[Supabase] Failed (HTTP %d)\n", httpCode);
  if (httpCode < 0) {
    Serial.println("[Supabase] Hint: Check URL & internet connection.");
  } else if (httpCode == 401) {
    Serial.println("[Supabase] Hint: Invalid SUPABASE_KEY — check anon key.");
  } else if (httpCode == 404) {
    Serial.println("[Supabase] Hint: Table not found — check SUPABASE_URL.");
  } else if (httpCode == 400) {
    Serial.println("[Supabase] Hint: Bad request body: " + response);
  }
  return false;
}

// ─────────────────────────────────────────────────────────────
//  HELPER: printDivider
// ─────────────────────────────────────────────────────────────
void printDivider() {
  Serial.println("─────────────────────────────────────────────");
}
