/*
 * ==============================================================================
 * Project: IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
 * Platform: ESP32 Dev Module
 * Sensors:
 *   - MQ-3   (Alcohol / Hydrocarbons / Methane trace) -> Analog Pin (GPIO 34)
 *   - MQ-7   (Carbon Monoxide - CO)                  -> Analog Pin (GPIO 35)
 *   - MQ-135 (Air Quality / NH3 / NOx / Smoke)       -> Analog Pin (GPIO 32)
 *   - DHT11  (Temperature & Humidity)                -> Digital Pin (GPIO 4)
 *   - GPS Neo-6M (Location & Geofencing)             -> HardwareSerial2 (RX: 16, TX: 17)
 *   - MPU-6050 (6-axis Accelerometer & Gyroscope)    -> I2C (SDA: 21, SCL: 22)
 * Actuators / Indicators:
 *   - Buzzer                                         -> GPIO 25
 *   - Danger LED                                     -> GPIO 26
 * Cloud Connectivity:
 *   - WiFi (Hotspot) + Supabase REST API (HTTPS POST)
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
// (Fill these with your credentials later)
// ==========================================
const char* WIFI_SSID     = "YOUR_HOTSPOT_NAME";       // Replace with your Hotspot Name
const char* WIFI_PASSWORD = "YOUR_HOTSPOT_PASSWORD";   // Replace with your Hotspot Password

// Supabase REST Endpoint: https://<PROJECT_ID>.supabase.co/rest/v1/helmet_telemetry
const char* SUPABASE_URL  = "https://YOUR_PROJECT_ID.supabase.co/rest/v1/helmet_telemetry";
const char* SUPABASE_KEY  = "YOUR_SUPABASE_ANON_KEY";  // Replace with your Anon API key

const char* WORKER_ID     = "W-101";                   // Unique Helmet ID

// ==========================================
// 2. PIN DEFINITIONS
// ==========================================
#define PIN_MQ3      34   // ADC1_CH6
#define PIN_MQ7      35   // ADC1_CH7
#define PIN_MQ135    32   // ADC1_CH4
#define PIN_DHT      4    // DHT11 Data
#define PIN_BUZZER   25   // Active Buzzer
#define PIN_LED      26   // Alert Indicator LED

#define DHTTYPE      DHT11
#define GPS_RX_PIN   16   // Connect to GPS TX
#define GPS_TX_PIN   17   // Connect to GPS RX

// ==========================================
// 3. THRESHOLDS & GEOFENCE SPECS
// ==========================================
const float TEMP_DANGER_CELSIUS  = 42.0;
const int   MQ7_DANGER_PPM       = 50;   // Lethal Carbon Monoxide
const int   MQ135_DANGER_PPM     = 250;  // Severe Air Contamination
const float MQ3_DANGER_MGL       = 0.40; // Flammable Gas
const float FALL_THRESHOLD_G     = 2.80; // Total acceleration > 2.8G indicates fall shock
const unsigned long INACTIVITY_LIMIT_MS = 60000; // 60 seconds without motion

// Safe Mine Site Geofence Center (Latitude, Longitude)
const double MINE_CENTER_LAT = 12.971598;
const double MINE_CENTER_LNG = 77.594566;
const double SAFE_RADIUS_METERS = 180.0;

// ==========================================
// 4. OBJECT INSTANCES & TIMERS
// ==========================================
DHT dht(PIN_DHT, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2
Adafruit_MPU6050 mpu;

unsigned long lastTelemetryUpload = 0;
const unsigned long UPLOAD_INTERVAL_MS = 3000; // Send payload every 3 seconds

unsigned long lastMotionDetectedTime = 0;
bool isFallTriggered = false;
bool isGeofenceBreached = false;

// ==========================================
// 5. HELPER: Haversine Distance (Meters)
// ==========================================
double calculateDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  double R = 6371000.0; // Earth radius in meters
  double dLat = (lat2 - lat1) * DEG_TO_RAD;
  double dLon = (lon2 - lon1) * DEG_TO_RAD;
  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
             sin(dLon / 2.0) * sin(dLon / 2.0);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return R * c;
}

// ==========================================
// 6. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[MineGuard] Initializing Smart Safety Helmet...");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  // Initialize DHT11
  dht.begin();
  Serial.println("[MineGuard] DHT11 Initialized.");

  // Initialize GPS Serial
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[MineGuard] GPS Neo-6M Serial Initialized.");

  // Initialize MPU-6050
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("[MineGuard] Warning: MPU6050 not detected. Continuing with mock values if needed.");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("[MineGuard] MPU-6050 Initialized.");
  }

  // Connect to WiFi
  connectWiFi();
  lastMotionDetectedTime = millis();
}

// ==========================================
// 7. MAIN LOOP
// ==========================================
void loop() {
  // Feed GPS parser
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Read Motion & Inactivity (MPU-6050)
  sensors_event_t a, g, temp_mpu;
  float totalAccelG = 1.0;
  if (mpu.getEvent(&a, &g, &temp_mpu)) {
    float ax = a.acceleration.x / 9.81;
    float ay = a.acceleration.y / 9.81;
    float az = a.acceleration.z / 9.81;
    totalAccelG = sqrt(ax * ax + ay * ay + az * az);

    // Fall impact check
    if (totalAccelG >= FALL_THRESHOLD_G) {
      isFallTriggered = true;
      Serial.println("[ALERT] High-G impact detected! Fall event triggered.");
    }

    // Motion detection: if variation from 1G is observed, worker is active
    if (fabs(totalAccelG - 1.0) > 0.15) {
      lastMotionDetectedTime = millis(); // Reset inactivity timer
    }
  }

  // Evaluate Inactivity
  unsigned long inactivityDuration = millis() - lastMotionDetectedTime;
  bool isInactiveAlarm = (inactivityDuration >= INACTIVITY_LIMIT_MS);

  // Read Gas Sensors (Analog Conversion)
  int rawMQ3   = analogRead(PIN_MQ3);
  int rawMQ7   = analogRead(PIN_MQ7);
  int rawMQ135 = analogRead(PIN_MQ135);

  float mq3GasPPM  = (rawMQ3 / 4095.0) * 0.8;    // Approximate mg/L
  float mq7CoPPM   = (rawMQ7 / 4095.0) * 100.0;  // Approximate PPM
  float mq135AirPPM= (rawMQ135 / 4095.0) * 400.0;// Approximate Air Quality PPM

  // Read DHT11 Climate
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  if (isnan(temperature)) temperature = 28.0;
  if (isnan(humidity)) humidity = 60.0;

  // Read GPS
  double currentLat = MINE_CENTER_LAT;
  double currentLng = MINE_CENTER_LNG;
  if (gps.location.isValid()) {
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
  }

  // Evaluate Geofence Breach
  double distFromBase = calculateDistanceMeters(currentLat, currentLng, MINE_CENTER_LAT, MINE_CENTER_LNG);
  isGeofenceBreached = (distFromBase > SAFE_RADIUS_METERS);

  // Evaluate Overall Danger State
  bool isCritical = (mq7CoPPM >= MQ7_DANGER_PPM) ||
                    (mq135AirPPM >= MQ135_DANGER_PPM) ||
                    (mq3GasPPM >= MQ3_DANGER_MGL) ||
                    (temperature >= TEMP_DANGER_CELSIUS) ||
                    isFallTriggered ||
                    isInactiveAlarm ||
                    isGeofenceBreached;

  // Local Audio / Visual Alert Trigger
  if (isCritical) {
    digitalWrite(PIN_BUZZER, HIGH);
    digitalWrite(PIN_LED, HIGH);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED, LOW);
  }

  // Transmit Telemetry to Supabase Cloud periodically
  if (millis() - lastTelemetryUpload >= UPLOAD_INTERVAL_MS) {
    lastTelemetryUpload = millis();
    sendTelemetryToSupabase(
      temperature, humidity, mq7CoPPM, mq135AirPPM, mq3GasPPM,
      currentLat, currentLng, totalAccelG, isFallTriggered, (inactivityDuration / 1000),
      isCritical ? "DANGER" : "SAFE"
    );
  }

  delay(50);
}

// ==========================================
// 8. WIFI CONNECTION
// ==========================================
void connectWiFi() {
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Connection failed. Running in offline/standalone alert mode.");
  }
}

// ==========================================
// 9. SUPABASE REST API DISPATCH (HTTPS POST)
// ==========================================
void sendTelemetryToSupabase(
  float temp, float humid, float mq7, float mq135, float mq3,
  double lat, double lng, float accel, bool fall, int inactivitySecs, const char* status
) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SUPABASE_URL);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");

  // Construct JSON Body
  String jsonBody = "{";
  jsonBody += "\"worker_id\":\"" + String(WORKER_ID) + "\",";
  jsonBody += "\"temperature\":" + String(temp, 2) + ",";
  jsonBody += "\"humidity\":" + String(humid, 2) + ",";
  jsonBody += "\"mq7_co\":" + String(mq7, 1) + ",";
  jsonBody += "\"mq135_air\":" + String(mq135, 1) + ",";
  jsonBody += "\"mq3_gas\":" + String(mq3, 2) + ",";
  jsonBody += "\"latitude\":" + String(lat, 6) + ",";
  jsonBody += "\"longitude\":" + String(lng, 6) + ",";
  jsonBody += "\"accel_total\":" + String(accel, 2) + ",";
  jsonBody += "\"is_fall\":" + String(fall ? "true" : "false") + ",";
  jsonBody += "\"inactivity_secs\":" + String(inactivitySecs) + ",";
  jsonBody += "\"alert_level\":\"" + String(status) + "\"";
  jsonBody += "}";

  int httpCode = http.POST(jsonBody);
  if (httpCode >= 200 && httpCode < 300) {
    Serial.println("[Supabase] Telemetry sent successfully!");
  } else {
    Serial.print("[Supabase] POST failed. HTTP Code: ");
    Serial.println(httpCode);
  }

  http.end();
}
