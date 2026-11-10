/*
 * ==============================================================================
 * Project: IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 * Platform: ESP32 Dev Module
 * 
 * SENSOR ARRAY & WIRING:
 *   - DHT11      (Temperature & Humidity)             -> GPIO 4
 *   - MQ-2       (Smoke / LPG / Flammable Gas)        -> GPIO 34 (ADC1_CH6)
 *   - MQ-7       (Carbon Monoxide - CO)               -> GPIO 35 (ADC1_CH7)
 *   - MQ-135     (Air Quality / Hazardous Gases)      -> GPIO 32 (ADC1_CH4)
 *   - GPS Neo-6M (Location & Geofencing)              -> UART2 (RX2: GPIO 16, TX2: GPIO 17)
 *   - MPU-6050   (6-Axis Fall & Inactivity Tracker)   -> I2C (SDA: GPIO 21, SCL: GPIO 22)
 * 
 * ACTUATORS:
 *   - Active Buzzer                                   -> GPIO 25
 *   - Alert LED (with 220Ω resistor)                  -> GPIO 26
 * 
 * PINOUT REMINDER ON ESP32 BOARD:
 *   - GPIO 16 is labeled "RX2" (under D4 on left rail)
 *   - GPIO 17 is labeled "TX2" (under RX2 on left rail)
 *   - GPS TX connects to ESP32 RX2 (GPIO 16)
 *   - GPS RX connects to ESP32 TX2 (GPIO 17)
 *   - MPU-6050 SDA connects to GPIO 21, SCL connects to GPIO 22
 * ==============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <DHT.h>
#include <TinyGPSPlus.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "soc/soc.h"             // Power management
#include "soc/rtc_cntl_reg.h"    // Disable brownout reset detector

// ==========================================
// 1. NETWORK & SUPABASE CONFIGURATION
// ==========================================
const char* WIFI_SSID     = "RH-2.4G-99FC50";
const char* WIFI_PASSWORD = "44953B99FC50";

const char* SUPABASE_URL  = "https://srkowkuclkimtwhkiutg.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY  = "sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu";
const char* WORKER_ID     = "W-101";

// ==========================================
// 2. PIN DEFINITIONS
// ==========================================
#define PIN_DHT       4
#define DHTTYPE       DHT11

#define PIN_MQ2      34       // MQ-2 Smoke / Flammable Gas
#define PIN_MQ7      35       // MQ-7 Carbon Monoxide
#define PIN_MQ135    32       // MQ-135 Air Quality

#define GPS_RX_PIN   16       // ESP32 RX2 <-- NEO-6M TX
#define GPS_TX_PIN   17       // ESP32 TX2 <-- NEO-6M RX
#define GPS_BAUD     9600

#define PIN_BUZZER   25
#define PIN_LED      26

// ==========================================
// 3. CALIBRATION & THRESHOLDS
// ==========================================
const float MQ2_SCALE     = 3030.0; // 3.3V full-scale ≈ 10,000 PPM
const float MQ7_SCALE     =   30.0; // 3.3V full-scale ≈ 100 PPM
const float MQ135_SCALE   =   30.0; // 3.3V full-scale ≈ 100 PPM

const float TEMP_DANGER_C     = 40.0; // °C
const float TEMP_WARNING_C    = 35.0; // °C
const float HUMID_DANGER      = 90.0; // %RH

const float MQ2_DANGER_PPM    = 10000.0;
const float MQ2_WARNING_PPM   =  5000.0;
const float MQ7_DANGER_PPM    =    50.0;
const float MQ7_WARNING_PPM   =    35.0;
const float MQ135_DANGER_PPM  =    50.0;
const float MQ135_WARNING_PPM =    25.0;

const float FALL_THRESHOLD_G  =  2.80; // High-G impact shock trigger
const unsigned long INACTIVITY_LIMIT_MS = 60000; // 60s Man-Down timeout

// Fallback Coordinates (Mine Site Center)
const double MINE_CENTER_LAT    = 12.971598;
const double MINE_CENTER_LNG    = 77.594566;
const double SAFE_RADIUS_METERS = 180.0;

// ==========================================
// 4. OBJECT INSTANCES & TIMERS
// ==========================================
DHT dht(PIN_DHT, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);   // UART2
Adafruit_MPU6050 mpu;

unsigned long lastTelemetryUpload    = 0;
const unsigned long UPLOAD_INTERVAL_MS = 3000;

unsigned long lastMotionDetectedTime = 0;
unsigned long buzzerPatternStart     = 0;
unsigned long lastGpsLogTime         = 0;

bool isFallTriggered    = false;
bool isGeofenceBreached = false;
bool mpuAvailable       = false;
bool prevDanger         = false;

// ==========================================
// 5. HELPER FUNCTIONS
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

float readMQ(int pin, float scaleFactor) {
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (sum / 8.0) * (3.3 / 4095.0) * scaleFactor;
}

void runDangerBuzzerPattern() {
  unsigned long phase = (millis() - buzzerPatternStart) % 1500;
  // Rapid double-beep: ON 120ms | OFF 100ms | ON 120ms | OFF 1160ms
  if (phase < 120 || (phase >= 220 && phase < 340)) {
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}

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

// ==========================================
// 6. SUPABASE DISPATCH
// ==========================================
bool sendTelemetryToSupabase(
  float temp, float humid,
  float mq2, float mq7, float mq135,
  double lat, double lng,
  float accel, bool fall, int inactivitySecs,
  int sats, float hdop, bool gpsFix,
  const String& alertLevel
) {
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

  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer",        "return=minimal");

  String body = "{";
  body += "\"worker_id\":\""       + String(WORKER_ID)               + "\",";
  body += "\"temperature\":"       + String(temp, 2)                 + ",";
  body += "\"humidity\":"          + String(humid, 2)                + ",";
  body += "\"mq2_smoke\":"         + String(mq2, 2)                  + ",";
  body += "\"mq7_co\":"            + String(mq7, 2)                  + ",";
  body += "\"mq135_air\":"         + String(mq135, 2)                + ",";
  body += "\"mq3_gas\":0.05,";                                         // Safe constant to avoid legacy column numeric overflow
  body += "\"latitude\":"          + String(lat, 6)                  + ",";
  body += "\"longitude\":"         + String(lng, 6)                  + ",";
  body += "\"accel_total\":"       + String(accel, 2)                + ",";
  body += "\"is_fall\":"           + String(fall ? "true" : "false") + ",";
  body += "\"inactivity_secs\":"   + String(inactivitySecs)          + ",";
  body += "\"gps_satellites\":"    + String(sats)                    + ",";
  body += "\"gps_hdop\":"          + String(hdop, 1)                 + ",";
  body += "\"gps_fix\":"           + String(gpsFix ? "true" : "false") + ",";
  body += "\"alert_level\":\""     + alertLevel                      + "\"";
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

// ==========================================
// 7. WIFI CONNECTION (With Google DNS Fallback)
// ==========================================
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);

  // Set Google Public DNS to avoid mobile hotspot / router DNS resolution failures
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
    Serial.println("[WiFi] Connection failed. Check SSID & password.");
  }
}

// ==========================================
// 8. SETUP
// ==========================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector to prevent restart loops
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[MineGuard] ============================================");
  Serial.println("[MineGuard] Smart Safety Helmet All-In-One System Booting...");
  Serial.println("[MineGuard] ============================================");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);

  // 1. DHT11
  dht.begin();
  Serial.println("[Init] DHT11 Initialized on GPIO 4.");

  // 2. GPS NEO-6M
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(100);
  Serial.printf("[Init] GPS Neo-6M on RX=GPIO%d (RX2), TX=GPIO%d (TX2) @ %d baud\n",
                GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);

  // 3. MPU-6050 (Auto-detected on I2C)
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("[Init] Note: MPU-6050 not detected. Continuing with other sensors.");
    mpuAvailable = false;
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuAvailable = true;
    Serial.println("[Init] MPU-6050 Motion Sensor Initialized on SDA:21, SCL:22.");
  }

  // 4. WiFi
  connectWiFi();

  lastMotionDetectedTime = millis();
  buzzerPatternStart     = millis();
  lastTelemetryUpload    = millis();

  Serial.println("[Init] Boot complete. Entering live monitoring loop.\n");
}

// ==========================================
// 9. MAIN LOOP
// ==========================================
void loop() {

  // 1. Feed GPS stream to TinyGPSPlus
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // 2. Motion & Fall Detection (MPU-6050)
  float totalAccelG = 1.0;
  if (mpuAvailable) {
    sensors_event_t a, g_ev, temp_mpu;
    if (mpu.getEvent(&a, &g_ev, &temp_mpu)) {
      float ax = a.acceleration.x / 9.80665;
      float ay = a.acceleration.y / 9.80665;
      float az = a.acceleration.z / 9.80665;
      totalAccelG = sqrt(ax * ax + ay * ay + az * az);

      if (totalAccelG >= FALL_THRESHOLD_G) {
        isFallTriggered = true;
        Serial.printf("[ALERT] Fall Shock Impact Detected: %.2f G!\n", totalAccelG);
      }
      if (fabs(totalAccelG - 1.0) > 0.15) {
        lastMotionDetectedTime = millis(); // Reset inactivity timer on movement
      }
    }
  }

  unsigned long inactivityDuration = mpuAvailable ? (millis() - lastMotionDetectedTime) : 0;
  bool isInactiveAlarm = mpuAvailable && (inactivityDuration >= INACTIVITY_LIMIT_MS);

  // 3. Continuous buzzer pattern if dangerous state
  if (prevDanger) {
    runDangerBuzzerPattern();
  }

  // 4. Telemetry Read & Upload Cycle (every 3 seconds)
  if (millis() - lastTelemetryUpload >= UPLOAD_INTERVAL_MS) {
    lastTelemetryUpload = millis();

    // Read Climate (DHT11)
    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();
    if (isnan(temperature)) temperature = 27.5;
    if (isnan(humidity))    humidity    = 60.0;

    // Read Gas Array (MQ-2, MQ-7, MQ-135)
    float mq2PPM   = readMQ(PIN_MQ2,   MQ2_SCALE);
    float mq7PPM   = readMQ(PIN_MQ7,   MQ7_SCALE);
    float mq135PPM = readMQ(PIN_MQ135, MQ135_SCALE);

    // Read GPS Telemetry
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

    // Geofencing Check (when fix acquired)
    double distFromBase = calculateDistanceMeters(currentLat, currentLng, MINE_CENTER_LAT, MINE_CENTER_LNG);
    isGeofenceBreached  = gpsFixed && (distFromBase > SAFE_RADIUS_METERS);

    // Hazard Level Evaluation
    bool isDanger = (temperature  >= TEMP_DANGER_C)   ||
                    (humidity     >= HUMID_DANGER)    ||
                    (mq2PPM       >= MQ2_DANGER_PPM)  ||
                    (mq7PPM       >= MQ7_DANGER_PPM)  ||
                    (mq135PPM     >= MQ135_DANGER_PPM)||
                    isFallTriggered || isInactiveAlarm || isGeofenceBreached;

    bool isWarning = (temperature  >= TEMP_WARNING_C)  ||
                     (mq2PPM       >= MQ2_WARNING_PPM) ||
                     (mq7PPM       >= MQ7_WARNING_PPM) ||
                     (mq135PPM     >= MQ135_WARNING_PPM);

    String alertLevel = isDanger ? "DANGER" : (isWarning ? "WARNING" : "SAFE");

    // Local Alarm Indicators
    handleBuzzerAndLED(alertLevel);
    prevDanger = isDanger;

    // Print summary to Serial Monitor
    Serial.printf("[Sensors] Temp: %.1f C | Hum: %.1f %% | MQ-2: %.0f PPM | MQ-7: %.0f PPM | MQ-135: %.0f PPM\n",
                  temperature, humidity, mq2PPM, mq7PPM, mq135PPM);
    Serial.printf("[GPS]     Lat: %.6f, Lng: %.6f | Sats: %d | HDOP: %.1f | Fix: %s\n",
                  currentLat, currentLng, gpsSats, gpsHDOP, gpsFixed ? "YES" : "NO");
    if (mpuAvailable) {
      Serial.printf("[Motion]  Accel: %.2f G | Inactivity: %lu s | Fall: %s\n",
                    totalAccelG, inactivityDuration / 1000, isFallTriggered ? "YES" : "NO");
    }
    Serial.printf("[Status]  Alert Level: %s\n", alertLevel.c_str());

    // Send payload to Supabase
    bool ok = sendTelemetryToSupabase(
      temperature, humidity, mq2PPM, mq7PPM, mq135PPM,
      currentLat, currentLng, totalAccelG,
      isFallTriggered, (int)(inactivityDuration / 1000),
      gpsSats, gpsHDOP, gpsFixed,
      alertLevel
    );

    // Reset one-shot fall flag after reporting
    isFallTriggered = false;
    Serial.println("─────────────────────────────────────────────────────────");
  }
}
