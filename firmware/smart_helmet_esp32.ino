/*
 * ==============================================================================
 * Project: IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 * Platform: ESP32 Dev Module
 * Sensors:
 *   - MQ-3   (Alcohol / Hydrocarbons / Methane trace) -> Analog Pin (GPIO 34)
 *   - MQ-7   (Carbon Monoxide - CO)                   -> Analog Pin (GPIO 35)
 *   - MQ-135 (Air Quality / NH3 / NOx / Smoke)        -> Analog Pin (GPIO 32)
 *   - DHT11  (Temperature & Humidity)                 -> Digital Pin (GPIO 4)
 *   - GPS Neo-6M (Location & Geofencing)              -> HardwareSerial2 (RX: 16, TX: 17)
 *   - MPU-6050 (6-axis Accelerometer & Gyroscope)     -> I2C (SDA: 21, SCL: 22)
 * Actuators / Indicators:
 *   - Buzzer                                          -> GPIO 25
 *   - Danger LED                                      -> GPIO 26
 * Cloud Connectivity:
 *   - WiFi (Hotspot) + Supabase REST API (HTTPS POST)
 *
 * GPS NEO-6M WIRING:
 *   NEO-6M VCC  -> ESP32 3.3V  (or 5V if module has onboard regulator)
 *   NEO-6M GND  -> ESP32 GND
 *   NEO-6M TX   -> ESP32 GPIO 16  (UART2 RX)
 *   NEO-6M RX   -> ESP32 GPIO 17  (UART2 TX)
 * ==============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <DHT.h>
#include <TinyGPSPlus.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ==========================================
// 1. NETWORK & SUPABASE CONFIGURATION
// ==========================================
const char* WIFI_SSID     = "darshan";
const char* WIFI_PASSWORD = "12345678";

const char* SUPABASE_URL  = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY  = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";
const char* WORKER_ID     = "W-101";

// ==========================================
// 2. PIN DEFINITIONS
// ==========================================
#define PIN_MQ3      34
#define PIN_MQ7      35
#define PIN_MQ135    32
#define PIN_DHT       4
#define PIN_BUZZER   25
#define PIN_LED      26

#define DHTTYPE      DHT11
#define GPS_RX_PIN   16   // ESP32 RX2 <-- NEO-6M TX
#define GPS_TX_PIN   17   // ESP32 TX2 <-- NEO-6M RX
#define GPS_BAUD     9600

// ==========================================
// 3. GPS DEBUG MODE
// Set to true to mirror raw NMEA sentences
// to Serial Monitor for wiring diagnosis.
// Set to false for normal operation.
// ==========================================
#define GPS_DEBUG  false

// ==========================================
// 4. THRESHOLDS & GEOFENCE SPECS
// ==========================================
const float TEMP_DANGER_CELSIUS       = 40.0;  // Alert threshold lowered to 40°C
const int   MQ7_DANGER_PPM            = 50;
const int   MQ135_DANGER_PPM          = 250;
const float MQ3_DANGER_MGL            = 0.40;
const float FALL_THRESHOLD_G          = 2.80;
const unsigned long INACTIVITY_LIMIT_MS = 60000;

const double MINE_CENTER_LAT    = 12.971598;
const double MINE_CENTER_LNG    = 77.594566;
const double SAFE_RADIUS_METERS = 180.0;

// ==========================================
// 5. OBJECT INSTANCES & TIMERS
// ==========================================
DHT dht(PIN_DHT, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);   // UART2
Adafruit_MPU6050 mpu;

unsigned long lastTelemetryUpload    = 0;
const unsigned long UPLOAD_INTERVAL_MS = 3000;

unsigned long lastMotionDetectedTime = 0;
unsigned long lastBuzzerPatternTime  = 0;  // Timer for temperature buzzer pattern
bool          tempAlertActive        = false;  // True while temp >= TEMP_DANGER_CELSIUS
unsigned long lastGpsLogTime         = 0;

bool isFallTriggered    = false;
bool isGeofenceBreached = false;
bool mpuAvailable       = false;

// ==========================================
// 6. HELPER: Haversine Distance (Meters)
// ==========================================
double calculateDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = (lat2 - lat1) * DEG_TO_RAD;
  double dLon = (lon2 - lon1) * DEG_TO_RAD;
  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
             sin(dLon / 2.0) * sin(dLon / 2.0);
  return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// ==========================================
// 7. FUNCTION DECLARATIONS
// ==========================================
void connectWiFi();
void runTemperatureBuzzerPattern();  // Rapid double-beep for heat alerts
void sendTelemetryToSupabase(
  float temp, float humid, float mq7, float mq135, float mq3,
  double lat, double lng, float accel, bool fall, int inactivitySecs,
  int sats, float hdop, bool gpsFix, const char* status
);

// ==========================================
// 8. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[MineGuard] ============================================");
  Serial.println("[MineGuard] Smart Safety Helmet Booting...");
  Serial.println("[MineGuard] ============================================");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  // --- DHT11 ---
  dht.begin();
  Serial.println("[MineGuard] DHT11 Initialized.");

  // --- GPS NEO-6M ---
  // IMPORTANT: Always pass RX and TX pins explicitly for ESP32 UART2
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(100);
  Serial.printf("[GPS] UART2 started on RX=GPIO%d, TX=GPIO%d @ %d baud\n",
                GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  Serial.println("[GPS] Waiting for satellite fix... (30-120 sec outdoors)");

  // --- MPU-6050 ---
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("[MineGuard] Warning: MPU6050 not found. Motion tracking disabled.");
    mpuAvailable = false;
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuAvailable = true;
    Serial.println("[MineGuard] MPU-6050 Initialized.");
  }

  // --- WiFi ---
  connectWiFi();
  lastMotionDetectedTime = millis();
  Serial.println("[MineGuard] Boot complete. Entering main loop.\n");
}

// ==========================================
// 9. MAIN LOOP
// ==========================================
void loop() {

  // Feed every byte from GPS into TinyGPSPlus parser
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    gps.encode(c);
    if (GPS_DEBUG) Serial.write(c);   // Mirror raw NMEA when debugging
  }

  // GPS Watchdog: print status every 5 seconds
  if (millis() - lastGpsLogTime >= 5000) {
    lastGpsLogTime = millis();
    if (gps.charsProcessed() < 10) {
      Serial.println("[GPS] WARNING: No data from GPS module!");
      Serial.println("      Check: NEO-6M TX -> ESP32 GPIO 16 (RX2)");
    } else if (!gps.location.isValid()) {
      Serial.printf("[GPS] Searching... Chars=%lu  Sats=%d  Fix=No\n",
                    gps.charsProcessed(), (int)gps.satellites.value());
    } else {
      Serial.printf("[GPS] FIX  Lat=%.6f  Lng=%.6f  Sats=%d  HDOP=%.1f  Age=%lums\n",
                    gps.location.lat(), gps.location.lng(),
                    (int)gps.satellites.value(),
                    gps.hdop.hdop(),
                    gps.location.age());
    }
  }

  // Motion & Fall Detection (MPU-6050)
  float totalAccelG = 1.0;
  if (mpuAvailable) {
    sensors_event_t a, g_ev, temp_mpu;
    if (mpu.getEvent(&a, &g_ev, &temp_mpu)) {
      float ax = a.acceleration.x / 9.81;
      float ay = a.acceleration.y / 9.81;
      float az = a.acceleration.z / 9.81;
      totalAccelG = sqrt(ax * ax + ay * ay + az * az);

      if (totalAccelG >= FALL_THRESHOLD_G) {
        isFallTriggered = true;
        Serial.println("[ALERT] High-G impact! Fall event triggered.");
      }
      if (fabs(totalAccelG - 1.0) > 0.15) {
        lastMotionDetectedTime = millis();
      }
    }
  }

  // Inactivity Evaluation
  unsigned long inactivityDuration = millis() - lastMotionDetectedTime;
  bool isInactiveAlarm = (inactivityDuration >= INACTIVITY_LIMIT_MS);

  // Gas Sensor Readings
  float mq3GasPPM   = (analogRead(PIN_MQ3)   / 4095.0) * 0.8;
  float mq7CoPPM    = (analogRead(PIN_MQ7)   / 4095.0) * 100.0;
  float mq135AirPPM = (analogRead(PIN_MQ135) / 4095.0) * 400.0;

  // DHT11 Climate
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  if (isnan(temperature)) temperature = 28.0;
  if (isnan(humidity))    humidity    = 60.0;

  // GPS Location & Quality
  double currentLat = MINE_CENTER_LAT;
  double currentLng = MINE_CENTER_LNG;
  int    gpsSats    = 0;
  float  gpsHDOP    = 99.9;
  bool   gpsFixed   = gps.location.isValid() && (gps.location.age() < 5000);

  if (gpsFixed) {
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
    gpsSats    = (int)gps.satellites.value();
    gpsHDOP    = (float)gps.hdop.hdop();
  }

  // Geofence Evaluation (only when GPS has a valid fix)
  double distFromBase = calculateDistanceMeters(currentLat, currentLng, MINE_CENTER_LAT, MINE_CENTER_LNG);
  isGeofenceBreached  = gpsFixed && (distFromBase > SAFE_RADIUS_METERS);

  // Overall Danger Assessment
  bool isTempDanger  = (temperature >= TEMP_DANGER_CELSIUS);
  bool isCritical    = (mq7CoPPM    >= MQ7_DANGER_PPM)    ||
                       (mq135AirPPM >= MQ135_DANGER_PPM)   ||
                       (mq3GasPPM   >= MQ3_DANGER_MGL)     ||
                       isTempDanger || isFallTriggered      ||
                       isInactiveAlarm || isGeofenceBreached;

  // ── Temperature-specific buzzer pattern (rapid double-beep every 1.5 s) ──
  // This gives workers a distinct audible cue that the hazard is HEAT,
  // rather than the continuous tone used for gas / fall / geofence alerts.
  if (isTempDanger) {
    if (!tempAlertActive) {
      tempAlertActive = true;
      Serial.printf("[TEMP ALERT] Temperature %.1f C exceeds %.1f C threshold!\n",
                    temperature, TEMP_DANGER_CELSIUS);
    }
    runTemperatureBuzzerPattern();   // Non-blocking pattern generator
  } else {
    tempAlertActive = false;
    // Only drive buzzer/LED with continuous ON if other hazards are active
    if (isCritical) {
      digitalWrite(PIN_BUZZER, HIGH);
      digitalWrite(PIN_LED,    HIGH);
    } else {
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_LED,    LOW);
    }
  }

  // LED always mirrors overall critical state
  if (isTempDanger) {
    digitalWrite(PIN_LED, HIGH);   // Keep LED solid ON during heat alert
  }

  // Supabase Telemetry Upload (every 3 seconds)
  if (millis() - lastTelemetryUpload >= UPLOAD_INTERVAL_MS) {
    lastTelemetryUpload = millis();
    // Determine alert string
    const char* alertStatus = isCritical ? "DANGER" : "SAFE";
    sendTelemetryToSupabase(
      temperature, humidity, mq7CoPPM, mq135AirPPM, mq3GasPPM,
      currentLat, currentLng, totalAccelG,
      isFallTriggered, (int)(inactivityDuration / 1000),
      gpsSats, gpsHDOP, gpsFixed,
      alertStatus
    );
    isFallTriggered = false;  // Reset one-shot fall flag after reporting
  }

  delay(50);
}

// ==========================================
// 9b. TEMPERATURE BUZZER PATTERN
//     Rapid double-beep: beep-beep ... pause ... repeat
//     Pattern: ON 120ms | OFF 100ms | ON 120ms | OFF 1160ms
//     Total cycle: ~1500 ms (1.5 seconds per double-beep)
//     Runs non-blocking using millis().
// ==========================================
void runTemperatureBuzzerPattern() {
  // Phase timing within the 1500 ms cycle (ms from cycle start)
  const unsigned long BEEP1_ON  = 0;
  const unsigned long BEEP1_OFF = 120;
  const unsigned long BEEP2_ON  = 220;
  const unsigned long BEEP2_OFF = 340;
  const unsigned long CYCLE_MS  = 1500;

  unsigned long phase = (millis() - lastBuzzerPatternTime) % CYCLE_MS;

  if (phase < BEEP1_OFF) {
    digitalWrite(PIN_BUZZER, HIGH);   // First beep ON
  } else if (phase < BEEP2_ON) {
    digitalWrite(PIN_BUZZER, LOW);    // Gap between beeps
  } else if (phase < BEEP2_OFF) {
    digitalWrite(PIN_BUZZER, HIGH);   // Second beep ON
  } else {
    digitalWrite(PIN_BUZZER, LOW);    // Silence until next cycle
  }
}

// ==========================================
// 10. WIFI CONNECTION
// ==========================================
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
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
    Serial.println("[WiFi] Connection failed. Running in offline/standalone alert mode.");
  }
}

// ==========================================
// 11. SUPABASE REST API DISPATCH
// ==========================================
void sendTelemetryToSupabase(
  float temp, float humid, float mq7, float mq135, float mq3,
  double lat, double lng, float accel,
  bool fall, int inactivitySecs,
  int sats, float hdop, bool gpsFix,
  const char* status
) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SUPABASE_URL);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  String body = "{";
  body += "\"worker_id\":\""     + String(WORKER_ID)            + "\",";
  body += "\"temperature\":"     + String(temp, 2)               + ",";
  body += "\"humidity\":"        + String(humid, 2)              + ",";
  body += "\"mq7_co\":"          + String(mq7, 1)                + ",";
  body += "\"mq135_air\":"       + String(mq135, 1)              + ",";
  body += "\"mq3_gas\":"         + String(mq3, 3)                + ",";
  body += "\"latitude\":"        + String(lat, 6)                + ",";
  body += "\"longitude\":"       + String(lng, 6)                + ",";
  body += "\"accel_total\":"     + String(accel, 2)              + ",";
  body += "\"is_fall\":"         + String(fall ? "true":"false") + ",";
  body += "\"inactivity_secs\":" + String(inactivitySecs)        + ",";
  body += "\"gps_satellites\":"  + String(sats)                  + ",";
  body += "\"gps_hdop\":"        + String(hdop, 1)               + ",";
  body += "\"gps_fix\":"         + String(gpsFix ? "true":"false") + ",";
  body += "\"alert_level\":\""   + String(status)                + "\"";
  body += "}";

  int httpCode = http.POST(body);
  if (httpCode >= 200 && httpCode < 300) {
    Serial.println("[Supabase] Telemetry sent OK.");
  } else {
    Serial.printf("[Supabase] POST failed. HTTP %d\n", httpCode);
    Serial.println("[Supabase] Response: " + http.getString());
  }
  http.end();
}
