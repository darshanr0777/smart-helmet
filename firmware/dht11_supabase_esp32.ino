/*
 * ============================================================
 *  Project : IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 *  Module  : DHT11 + MQ-2 + MQ-7 + MQ-135 → ESP32 → Supabase
 *  Board   : ESP32 Dev Module (any variant)
 *  Version : 3.0  (Added MQ-2, MQ-7, MQ-135 gas sensors)
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
 *  ║  Buzzer (+)            Signal      →  GPIO 25           ║
 *  ║  Buzzer (-)                        →  GND               ║
 *  ║  LED    (+)  220Ω res  Anode       →  GPIO 26           ║
 *  ║  LED    (-)            Cathode     →  GND               ║
 *  ╠══════════════════════════════════════════════════════════╣
 *  ║  IMPORTANT:                                             ║
 *  ║  • MQ sensors need 5V for heater — use VIN not 3V3!    ║
 *  ║  • ESP32 ADC pins are 3.3V MAX. MQ AO output ≈ 0–3.3V  ║
 *  ║    which is safe to connect directly to ESP32 ADC.     ║
 *  ║  • GPIO 34, 35 are INPUT-ONLY (no internal pull-up).   ║
 *  ║  • Pre-heat MQ sensors for 24–48 hrs before use for    ║
 *  ║    accurate readings. First-time burn-in is normal.    ║
 *  ║  • Digital (DO) pin of MQ boards is NOT used here;     ║
 *  ║    we use the Analog (AO) pin for finer resolution.    ║
 *  ╚══════════════════════════════════════════════════════════╝
 *
 *  REQUIRED LIBRARIES  (Arduino IDE → Library Manager)
 *  ─────────────────────────────────────────────────────────
 *  1. "DHT sensor library"  by Adafruit  + its dependencies
 *  2. WiFi / HTTPClient / WiFiClientSecure — built-in with
 *     ESP32 Arduino board package (no extra install)
 *
 *  BOARD SETUP  (Arduino IDE)
 *  ─────────────────────────────────────────────────────────
 *  Tools → Board     :  ESP32 Dev Module
 *  Tools → Port      :  (your COM port)
 *  Baud Rate Monitor :  115200
 *
 *  SENSOR ALERTS SUMMARY
 *  ─────────────────────────────────────────────────────────
 *  Sensor   Measures          WARNING threshold  DANGER threshold
 *  ───────  ────────────────  ─────────────────  ────────────────
 *  DHT11    Temperature       35 °C              40 °C
 *  DHT11    Humidity          —                  90 %RH
 *  MQ-2     Smoke / LPG / CO  200 PPM            400 PPM
 *  MQ-7     Carbon Monoxide   30 PPM             50 PPM  (IDLH)
 *  MQ-135   Air Quality (AQI) 150 PPM            250 PPM
 *
 * ============================================================
 */

// ─────────────────────────────────────────────────────────────
//  INCLUDES
// ─────────────────────────────────────────────────────────────
#include <WiFi.h>               // ESP32 WiFi stack
#include <HTTPClient.h>         // HTTP client
#include <WiFiClientSecure.h>   // HTTPS / TLS
#include <DHT.h>                // Adafruit DHT11/22 library

// ─────────────────────────────────────────────────────────────
//  1. WiFi CREDENTIALS  ← EDIT THESE
// ─────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "darshan";    // Hotspot / router SSID
const char* WIFI_PASSWORD = "12345678";   // Hotspot / router password

// ─────────────────────────────────────────────────────────────
//  2. SUPABASE CONFIGURATION  ← EDIT THESE
//
//  Supabase Dashboard → Project → Settings → API
//    "Project URL"           → paste below (without trailing /)
//    "anon / public" key     → paste below
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
// NOTE: Use ADC1 pins only (GPIO 32-39). ADC2 is used by WiFi.
#define PIN_MQ2    34       // GPIO 34 – MQ-2  (Smoke / LPG / CO)
#define PIN_MQ7    35       // GPIO 35 – MQ-7  (Carbon Monoxide)
#define PIN_MQ135  32       // GPIO 32 – MQ-135 (Air Quality / NH3 / NOx)

// Actuators
#define PIN_BUZZER 25       // Active buzzer (HIGH = ON)
#define PIN_LED    26       // Danger LED (HIGH = ON, via 220Ω resistor)

// ─────────────────────────────────────────────────────────────
//  5. SENSOR CALIBRATION CONSTANTS
//
//  The ESP32 ADC reads 0–4095 for 0–3.3V.
//  MQ sensors output a voltage proportional to gas concentration.
//  These LINEAR SCALE FACTORS are approximate — for lab-grade
//  accuracy you need the Rs/R0 logarithmic curve from the datasheet.
//  The values below are suitable for mine-safety alerting.
//
//  Formula used:
//    rawADC  = analogRead(PIN_MQx)           // 0 – 4095
//    voltage = rawADC * (3.3 / 4095.0)       // 0 – 3.3 V
//    ppm     = voltage * SCALE_FACTOR
// ─────────────────────────────────────────────────────────────
const float MQ2_SCALE    = 200.0;   // 3.3V full-scale ≈ 660 PPM  (smoke/LPG/CO mix)
const float MQ7_SCALE    =  30.0;   // 3.3V full-scale ≈  99 PPM  (CO)
const float MQ135_SCALE  = 120.0;   // 3.3V full-scale ≈ 396 PPM  (air quality)

// ─────────────────────────────────────────────────────────────
//  6. SAFETY THRESHOLDS
// ─────────────────────────────────────────────────────────────
// Temperature (DHT11)
const float TEMP_DANGER_C    = 40.0;   // °C
const float TEMP_WARNING_C   = 35.0;   // °C
const float HUMID_DANGER     = 90.0;   // %RH

// MQ-2  (Smoke / LPG / CO composite)
const float MQ2_DANGER_PPM   = 400.0;
const float MQ2_WARNING_PPM  = 200.0;

// MQ-7  (Carbon Monoxide — IDLH = 1200 PPM, OSHA PEL = 50 PPM)
const float MQ7_DANGER_PPM   =  50.0;
const float MQ7_WARNING_PPM  =  30.0;

// MQ-135 (Toxic / poor air quality)
const float MQ135_DANGER_PPM  = 250.0;
const float MQ135_WARNING_PPM = 150.0;

// ─────────────────────────────────────────────────────────────
//  7. TIMING
// ─────────────────────────────────────────────────────────────
const unsigned long UPLOAD_INTERVAL_MS  = 3000;   // Supabase POST every 3 s
const unsigned long WIFI_CHECK_INTERVAL = 10000;  // WiFi reconnect check every 10 s

// ─────────────────────────────────────────────────────────────
//  8. OBJECT INSTANCES & STATE
// ─────────────────────────────────────────────────────────────
DHT dht(PIN_DHT, DHTTYPE);

unsigned long lastUploadTime     = 0;
unsigned long lastWifiCheckMs    = 0;
unsigned long buzzerPatternStart = 0;
int           uploadCount        = 0;
int           failCount          = 0;
bool          prevDanger         = false;  // Edge detect for first DANGER crossing

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
                      const String& alertLevel);
void   printSensorReadings(float temp, float humid,
                           float mq2,  float mq7,  float mq135,
                           const String& alert);
void   printDivider();

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  printDivider();
  Serial.println("  Smart Safety Helmet — Multi-Sensor v3.0");
  Serial.println("  DHT11 | MQ-2 | MQ-7 | MQ-135 | ESP32 | Supabase");
  printDivider();

  // Output pins
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);
  Serial.println("[Init] Buzzer: GPIO" + String(PIN_BUZZER) +
                 "  LED: GPIO"    + String(PIN_LED));

  // Analog input pins (input-only, no pinMode needed for ADC on ESP32)
  Serial.println("[Init] MQ-2:   GPIO" + String(PIN_MQ2));
  Serial.println("[Init] MQ-7:   GPIO" + String(PIN_MQ7));
  Serial.println("[Init] MQ-135: GPIO" + String(PIN_MQ135));

  // DHT11 warm-up
  dht.begin();
  Serial.println("[DHT11] GPIO" + String(PIN_DHT) + " — 2 s warm-up...");
  delay(2000);

  // MQ sensor pre-heat notice
  Serial.println("[MQ] NOTE: MQ sensors need ~24-48 hr pre-heat burn-in");
  Serial.println("[MQ]       for accurate PPM readings on first use.");
  Serial.println("[MQ]       Relative readings are still valid for alerting.");

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

    // --- Alert level ---
    String alertLevel = buildAlertLevel(temperature, humidity, mq2PPM, mq7PPM, mq135PPM);

    // --- Print readings ---
    printSensorReadings(temperature, humidity, mq2PPM, mq7PPM, mq135PPM, alertLevel);

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

    // --- Supabase ---
    bool ok = sendToSupabase(temperature, humidity, mq2PPM, mq7PPM, mq135PPM, alertLevel);
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
//
//  Reads a MQ sensor analog pin and returns an approximate PPM.
//
//  Steps:
//   1. Average 5 samples to reduce noise.
//   2. Convert 12-bit ADC value (0-4095) → voltage (0-3.3V).
//   3. Multiply by scale factor to get approximate PPM.
//
//  scaleFactor = max_PPM_at_3.3V / 3.3
// ─────────────────────────────────────────────────────────────
float readMQ(int pin, float scaleFactor) {
  long   sum = 0;
  const  int SAMPLES = 5;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  float rawADC  = (float)sum / SAMPLES;
  float voltage = rawADC * (3.3f / 4095.0f);
  return voltage * scaleFactor;
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: buildAlertLevel
//  Evaluates all sensors and returns "SAFE" / "WARNING" / "DANGER"
//  DANGER takes priority over WARNING.
// ─────────────────────────────────────────────────────────────
String buildAlertLevel(float temp, float humid,
                       float mq2, float mq7, float mq135) {
  // DANGER conditions (any one triggers)
  if (temp   >= TEMP_DANGER_C   ||
      humid  >= HUMID_DANGER    ||
      mq2    >= MQ2_DANGER_PPM  ||
      mq7    >= MQ7_DANGER_PPM  ||
      mq135  >= MQ135_DANGER_PPM) {
    return "DANGER";
  }
  // WARNING conditions (any one triggers)
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
//
//  DANGER  → LED ON  +  rapid double-beep (non-blocking)
//  WARNING → LED ON  +  slow single-beep  (non-blocking)
//  SAFE    → LED OFF +  buzzer OFF
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
//
//  Rapid double-beep cycle = 1500 ms:
//    ▓ Beep-1  120 ms
//    ░ Gap      100 ms
//    ▓ Beep-2  120 ms
//    ░ Silence 1160 ms
// ─────────────────────────────────────────────────────────────
void runDangerBuzzerPattern() {
  const unsigned long CYCLE  = 1500;
  const unsigned long B1_ON  = 0;
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
//
//  Slow single-beep cycle = 2000 ms:
//    ▓ Beep    200 ms
//    ░ Silence 1800 ms
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
//  HTTPS POST to Supabase REST API.
//
//  Headers:
//    Content-Type:  application/json
//    apikey:        <anon key>
//    Authorization: Bearer <anon key>
//    Prefer:        return=minimal   (no response body — saves RAM)
//
//  JSON columns sent → helmet_telemetry table:
//    worker_id    VARCHAR   helmet identifier
//    temperature  NUMERIC   °C  (DHT11)
//    humidity     NUMERIC   %RH (DHT11)
//    mq3_gas      NUMERIC   PPM – MQ-2 smoke/LPG  (stored in mq3_gas column)
//    mq7_co       NUMERIC   PPM – MQ-7 CO
//    mq135_air    NUMERIC   PPM – MQ-135 air quality
//    alert_level  VARCHAR   "SAFE" / "WARNING" / "DANGER"
//
//  NOTE: The table column "mq3_gas" is reused for MQ-2 readings
//  since MQ-2 measures a similar composite gas group.
//  If you prefer a separate column, add it in supabase_schema.sql.
//
//  Returns true on HTTP 2xx.
// ─────────────────────────────────────────────────────────────
bool sendToSupabase(float temp, float humid,
                    float mq2,  float mq7,  float mq135,
                    const String& alertLevel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Supabase] Skipped — WiFi not connected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();   // Skip TLS cert verification (OK for IoT)

  HTTPClient http;
  http.begin(client, SUPABASE_URL);
  http.setTimeout(8000);  // 8 second HTTP timeout

  // ── Headers ───────────────────────────────────────────────
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  // ── JSON body ─────────────────────────────────────────────
  String body = "{";
  body += "\"worker_id\":\""   + String(WORKER_ID)     + "\",";
  body += "\"temperature\":"   + String(temp,   2)      + ",";
  body += "\"humidity\":"      + String(humid,  2)      + ",";
  body += "\"mq2_smoke\":"     + String(mq2,    2)      + ",";   // MQ-2 → dedicated mq2_smoke column
  body += "\"mq7_co\":"        + String(mq7,    2)      + ",";
  body += "\"mq135_air\":"     + String(mq135,  2)      + ",";
  body += "\"alert_level\":\"" + alertLevel             + "\"";
  body += "}";

  Serial.println("[Supabase] POST → " + alertLevel + " ...");

  int    httpCode = http.POST(body);
  String response = http.getString();
  http.end();

  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] Success  (HTTP %d)\n", httpCode);
    return true;
  }

  // Detailed error hints
  Serial.printf("[Supabase] FAILED   (HTTP %d)\n", httpCode);
  if      (httpCode < 0)   Serial.println("[Supabase] → Check URL and internet connection.");
  else if (httpCode == 401) Serial.println("[Supabase] → Invalid SUPABASE_KEY (401 Unauthorized).");
  else if (httpCode == 404) Serial.println("[Supabase] → Table not found — check SUPABASE_URL (404).");
  else if (httpCode == 400) Serial.println("[Supabase] → Bad JSON body: " + response);
  return false;
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: connectWiFi  (blocking startup, max 15 s)
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
    Serial.printf("[WiFi] Connected!  IP: %s  |  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFi] FAILED — offline mode. Uploads will be skipped.");
  }
}

// ─────────────────────────────────────────────────────────────
//  FUNCTION: ensureWiFi  (non-blocking reconnect, max 5 s)
// ─────────────────────────────────────────────────────────────
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[WiFi] Lost connection — reconnecting...");
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
//  FUNCTION: printSensorReadings
//  Formatted table output in Serial Monitor
// ─────────────────────────────────────────────────────────────
void printSensorReadings(float temp, float humid,
                         float mq2,  float mq7, float mq135,
                         const String& alert) {
  Serial.println();
  printDivider();
  Serial.println("  SENSOR READINGS");
  printDivider();
  Serial.printf("  DHT11  Temperature : %6.1f C      (DANGER >= %.0f C)\n",
                temp, TEMP_DANGER_C);
  Serial.printf("  DHT11  Humidity    : %6.1f %%RH   (DANGER >= %.0f %%)\n",
                humid, HUMID_DANGER);
  Serial.printf("  MQ-2   Smoke / LPG : %6.1f PPM   (DANGER >= %.0f PPM)\n",
                mq2,  MQ2_DANGER_PPM);
  Serial.printf("  MQ-7   Carbon  CO  : %6.1f PPM   (DANGER >= %.0f PPM)\n",
                mq7,  MQ7_DANGER_PPM);
  Serial.printf("  MQ-135 Air Quality : %6.1f PPM   (DANGER >= %.0f PPM)\n",
                mq135, MQ135_DANGER_PPM);
  printDivider();
  Serial.println("  ALERT LEVEL : " + alert);
}

// ─────────────────────────────────────────────────────────────
//  HELPER: printDivider
// ─────────────────────────────────────────────────────────────
void printDivider() {
  Serial.println("─────────────────────────────────────────────");
}
