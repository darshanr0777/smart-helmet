# Project Conversation & Implementation Archive

**Project Title:** IOT BASED SMART SAFETY HELMET FOR MINE WORKERS WITH REAL TIME MONITORING AND ALERT SYSTEM  
**GitHub Repository:** [https://github.com/darshanr0777/smart-helmet.git](https://github.com/darshanr0777/smart-helmet.git)  
**Date:** September 8, 2026  
**Author / Developer:** Darshan R (`darshanr0777`)

---

## 1. Project Requirements & Scope Summary

### 1.1 Hardware Specifications
- **Microcontroller:** ESP32 Dev Module (with built-in WiFi)
- **Sensor Array:**
  - **MQ-3:** Alcohol / Hydrocarbon / Methane trace detection (Analog ADC1_CH6 / GPIO 34)
  - **MQ-7:** Carbon Monoxide (CO) toxic gas detector (Analog ADC1_CH7 / GPIO 35)
  - **MQ-135:** Air Quality & Hazardous Industrial Gases: NH3, NOx, Alcohol, Benzene, Smoke, CO2 (Analog ADC1_CH4 / GPIO 32)
  - **DHT11:** Ambient temperature & relative humidity sensor (GPIO 4)
  - **GPS Neo-6M:** Real-time worker geolocation & geofencing coordinates (HardwareSerial2: RX 16, TX 17)
  - **MPU-6050:** 6-axis Accelerometer & Gyroscope for worker fall shock detection and abnormal prolonged inactivity/unconsciousness monitoring (I2C: SDA 21, SCL 22)
  - **Actuators:** Active Buzzer (GPIO 25) & Visual Alert LED (GPIO 26)

### 1.2 Web Application Features
- Cross-platform responsive dashboard designed for desktop control rooms and mobile smartphones.
- **Top KPI Cards:** Worker Health Status, Geofencing Boundary State, Activity/Posture, Gas Exposure Index.
- **Environmental Panel:** Dual circular gauges for DHT11 Temperature and Humidity with Heat-Stress warning.
- **Toxic Gas Array:** Progress meters with Safe, Warning, and Lethal badges for MQ-3, MQ-7, and MQ-135.
- **MPU-6050 Motion Panel:** Real-time 3-axis acceleration bars, G-force impact display, and 60-second inactivity counter with automatic "Man-Down" alarm.
- **Interactive Leaflet Map:**
  - Real-time GPS pinpoint with custom helmet worker marker.
  - Circular Authorized Mine Sector perimeter (180m radius).
  - Circular Restricted Hazard Shaft perimeter (70m radius) with automatic breach warning.
- **Web Audio Siren:** Synthesizer alarm sound with mute toggle.
- **Demo Simulator Controls:** Interactive buttons to simulate gas leaks, extreme heat, worker falls, inactivity, and safe reset.
- **Supabase Cloud Live Mode:** Integration modal to store project URL and API key for direct real-time telemetry streaming from ESP32.

---

## 2. Supabase Cloud Configuration

- **Supabase Project URL:** `https://srkowkuclkimtwhkiutg.supabase.co`
- **Supabase Publishable Key:** `sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu`
- **Database Tables:**
  - `helmet_telemetry`: Stores incoming sensor records from the ESP32.
  - `mine_alerts`: Logs historical hazard incidents and alerts.
- **Realtime Channel:** Enabled via publication `supabase_realtime` on `public.helmet_telemetry` and `public.mine_alerts`.

---

## 3. Circuit Pinout Reference Table

| Component | ESP32 GPIO Pin | Type / Interface |
| :--- | :--- | :--- |
| MQ-3 Gas Sensor | `GPIO 34` | Analog Input (ADC1) |
| MQ-7 CO Sensor | `GPIO 35` | Analog Input (ADC1) |
| MQ-135 Air Quality | `GPIO 32` | Analog Input (ADC1) |
| DHT11 Temp & Humidity | `GPIO 4` | Digital I/O |
| GPS Neo-6M TX | `GPIO 16` | UART2 RX |
| GPS Neo-6M RX | `GPIO 17` | UART2 TX |
| MPU-6050 SDA | `GPIO 21` | I2C Data |
| MPU-6050 SCL | `GPIO 22` | I2C Clock |
| Active Buzzer | `GPIO 25` | Digital Output |
| Alert LED | `GPIO 26` | Digital Output |

---

## 4. Created Project Files

1. **`index.html`**: Web dashboard markup, Leaflet map container, sensor gauges, and simulation controls.
2. **`style.css`**: Modern industrial dark theme, glassmorphism, responsive grid, and responsive styling for mobile and desktop.
3. **`app.js`**: Core dashboard logic, Haversine distance geofencing, Web Audio API alarm sound, simulation engine, and Supabase client listener.
4. **`firmware/smart_helmet_esp32.ino`**: ESP32 C++ firmware sketch for reading all 6 sensors, fall/inactivity computation, local alarm activation, and HTTPS REST payload dispatch to Supabase.
5. **`firmware/supabase_schema.sql`**: SQL migration script to set up tables, RLS policies, and Realtime publications.
6. **`README.md`**: Project documentation, quick-start guide, and circuit connections.

---

## 5. Git Commit History

- **Commit 1 (`8e52c1b`)**: `Initial commit: IoT Smart Safety Helmet web dashboard and ESP32 firmware`
- **Commit 2 (`10e8e9d`)**: `Configure Supabase URL and publishable key for real-time telemetry`
- **Branch:** `main`
- **Remote:** `origin https://github.com/darshanr0777/smart-helmet.git`

---

## 6. How to Push to GitHub

From your terminal or command prompt:
```cmd
cd "E:\darshan project"
git push -u origin main
```
Sign in when prompted by the GitHub browser authentication dialog.

---

## 7. Session Archive: November 7, 2026 — Vercel Dashboard Fixes, GPS Integration & Firmware v4.0

### 7.1 Problems Identified & Resolved

1. **Vercel Live Dashboard Freezing / Tabs Not Opening (`smart-helmet-chi.vercel.app`):**
   - **Root Cause:** A JavaScript syntax error in `app.js` (`SyntaxError: Identifier 'now' has already been declared` at line 701) prevented the entire script from executing. This caused the browser to ignore tab navigation, prevent Chart.js from rendering, and revert to default anchor hash navigation without switching views.
   - **Fix:** Renamed the second timestamp variable to `syncDate`. Verified zero errors via `node --check app.js`.
   - **Hash Routing Added:** Implemented `switchTab()` with support for direct URL hashes (`#live-map`, `#devices`, `#zones`, etc.) and a `hashchange` listener for browser back/forward navigation.
   - **Duplicate ID Cleanup:** Resolved duplicate HTML element IDs (`snapTempVal`, `snapHumidVal`, `snapTempBadge`) between the System Overview snapshot and the Devices tab.
   - **Defensive DOM Binding:** Wrapped event listeners in a `safeListen` helper so any missing DOM elements never crash subsequent JavaScript execution.
   - **Map Resize Watchdog:** Added staggered `mapInstance.invalidateSize(true)` triggers (at 50ms, 200ms, 450ms) so satellite hybrid map tiles render at 100% full width upon switching tabs.
   - **Vercel Cache Invalidation:** Added `vercel.json` with cache-control headers and bumped asset version query string to `?v=3.1` in `index.html`.

2. **GPS NEO-6M ESP32 Pinout Clarification:**
   - On the user's specific 30-pin ESP32 Dev Module, GPIO 16 and GPIO 17 are silkscreened as **`RX2`** and **`TX2`** (located immediately below `D4` on the left header rail).
   - **Wiring Confirmed:**
     - GPS `TX` ➔ ESP32 **`RX2`** (GPIO 16)
     - GPS `RX` ➔ ESP32 **`TX2`** (GPIO 17)
     - GPS `VCC` ➔ ESP32 **`VIN`** / `3V3`
     - GPS `GND` ➔ ESP32 **`GND`**

3. **Multi-Sensor Firmware Upgrade (v4.0):**
   - Upgraded `firmware/dht11_supabase_esp32.ino` to include **GPS NEO-6M** alongside DHT11, MQ-2, MQ-7, MQ-135, Buzzer, and LED.
   - Implemented non-blocking stream parsing via `TinyGPSPlus` in the main loop.
   - Added mine site fallback coordinates (`12.971598, 77.594566`) with `gps_fix: false` for indoor testing so the dashboard map stays centered while searching for satellites.
   - Added live GPS satellite count and fix status mapping in `app.js`.

4. **Supabase Schema & REST API Diagnostics:**
   - Updated `firmware/supabase_schema.sql` (Migration 5a) to ensure `latitude`, `longitude`, `gps_fix`, `gps_satellites`, and `gps_hdop` columns exist.
   - Verified direct endpoint dispatch with a test script — Supabase returned `HTTP 201 Created`.
   - Added detailed HTTP code and network error reporting to `sendToSupabase` on the ESP32.
   - Configured Google Public DNS (`8.8.8.8`, `8.8.4.4`) in `connectWiFi()` to prevent mobile hotspot / campus router DNS resolution failures.

---

### 7.2 Updated Git Commit History

- **Commit `53baced`**: `fix: resolve duplicate now identifier syntax error breaking Vercel dashboard and add hash-based tab routing`
- **Commit `fcddfaa`**: `feat(firmware): add GPS NEO-6M to multi-sensor sketch (v4.0) and update Supabase GPS migration schema`
- **Commit `6ca45a8`**: `feat(telemetry): map live GPS satellites and fix status from Supabase to dashboard`
- **Commit `b373b8e`**: `fix(firmware): add detailed HTTP error response and network diagnostics to sendToSupabase`
- **Commit `d2b00df`**: `fix(firmware): add Google Public DNS fallback to connectWiFi to prevent hotspot DNS lookup failures`

---

### 7.3 Next Steps for Next Session

1. **Test ESP32 Supabase Dispatch:** Check Serial Monitor output with the new diagnostic logging.
2. **Outdoor GPS Fix Test:** Place helmet / GPS antenna near an open window or outdoors to observe 1 Hz PPS LED blink and acquire 3D satellite lock.
3. **Integrate MPU-6050 (Final Sensor):**
   - Wire MPU-6050 to ESP32 I2C (`SDA` ➔ GPIO 21, `SCL` ➔ GPIO 22).
   - Enable high-G fall detection (`> 2.80G`) and 60-second inactivity Man-Down monitoring.

