# 🪖 IoT Based Smart Safety Helmet for Mine Workers with Real-Time Monitoring & Alert System

A smart industrial IoT safety system designed for underground mine workers, featuring real-time biometric and environmental telemetry, GPS geofencing, toxic gas hazard detection, worker fall shock detection, and prolonged inactivity/unconsciousness alarms.

---

## 🌟 Key Features

1. **Environmental & Climate Monitoring (DHT11)**
   - Continuous ambient temperature and relative humidity tracking.
   - Heat-stress alert when temperature exceeds safe thresholds (>42°C).

2. **Toxic & Flammable Gas Hazard Array**
   - **MQ-7**: Carbon Monoxide (CO) lethal gas detector (>50 PPM alarm).
   - **MQ-135**: Toxic air quality monitoring (Ammonia NH3, NOx, Smoke, Benzene >250 PPM alarm).
   - **MQ-3**: Hydrocarbons, alcohol, and flammable methane trace detection.

3. **Motion, Fall Impact & Prolonged Inactivity (MPU-6050 6-Axis IMU)**
   - High-G shock threshold detection (>2.80G) for instant worker fall alerts.
   - 60-second inactivity alarm (Man-Down emergency detection for unconsciousness or entrapment).

4. **Live GPS & Mine Geofencing (Neo-6M)**
   - Real-time pinpoint on interactive dark industrial map.
   - Circular and polygon geofence zones (Authorized Mine Sector vs Prohibited Deep Shaft Chamber).
   - Real-time audio and visual alert upon restricted perimeter breach.

5. **Cross-Platform Responsive Web Dashboard**
   - Works seamlessly on desktop laptops and mobile smartphones.
   - Audio siren synthesizer with mute toggle.
   - **Demo Simulator Mode**: Built-in test triggers so you can demonstrate the entire dashboard even before hardware is uploaded!
   - **Supabase Cloud Live Mode**: Direct real-time streaming from the ESP32.

---

## 🔌 Hardware Circuit Connections (ESP32)

| Sensor / Module | ESP32 Pin | Interface / Note |
| :--- | :--- | :--- |
| **MQ-3 Gas Sensor** | `GPIO 34` | Analog (ADC1_CH6) |
| **MQ-7 CO Sensor** | `GPIO 35` | Analog (ADC1_CH7) |
| **MQ-135 Air Quality** | `GPIO 32` | Analog (ADC1_CH4) |
| **DHT11 Temp & Humidity** | `GPIO 4` | Digital (10k pull-up) |
| **GPS Neo-6M TX** | `GPIO 16` | HardwareSerial2 RX |
| **GPS Neo-6M RX** | `GPIO 17` | HardwareSerial2 TX |
| **MPU-6050 SDA** | `GPIO 21` | I2C Data |
| **MPU-6050 SCL** | `GPIO 22` | I2C Clock |
| **Active Buzzer** | `GPIO 25` | Digital Output |
| **Alert Indicator LED**| `GPIO 26` | Digital Output (with 220Ω resistor) |

---

## 🚀 How to Run the Website

### Option 1: Direct File
Simply double-click or open `index.html` in any modern web browser (Chrome, Edge, Safari, Firefox).

### Option 2: Local HTTP Server (e.g. VS Code Live Server or Python)
```bash
python -m http.server 5500
```
Then navigate to `http://localhost:5500` on your laptop or phone.

---

## ☁️ Supabase Cloud Setup

1. Create a free project at [supabase.com](https://supabase.com).
2. Go to the **SQL Editor** in your Supabase dashboard.
3. Paste and run the contents of [`firmware/supabase_schema.sql`](firmware/supabase_schema.sql).
4. In your Web Dashboard, click the **Cloud** icon in the top right navbar, paste your **Supabase URL** and **Anon Key**, and switch the data source to **Supabase Live**.
5. In [`firmware/smart_helmet_esp32.ino`](firmware/smart_helmet_esp32.ino), fill in:
   - `WIFI_SSID` & `WIFI_PASSWORD`
   - `SUPABASE_URL` & `SUPABASE_KEY`
6. Flash the sketch to your ESP32!
