/**
 * MineGuard IoT Smart Safety Helmet - Real-time Dashboard & Alert Logic
 * Hardware Profile: ESP32, MQ-3, MQ-7, MQ-135, DHT11, GPS Neo-6M, MPU-6050
 */

// Global State
const state = {
  activeWorker: 'W-101',
  dataSource: 'sim', // 'sim' or 'supabase'
  sirenMuted: false,
  alarmActive: false,
  inactivitySeconds: 0,
  isFallDetected: false,
  isDeviceOnline: true,
  lastTelemetryTimestamp: Date.now(),
  lastTelemetryId: 0,
  
  // Geofencing Center & Mine Boundaries (Latitude, Longitude, Radius in meters)
  mineSiteCenter: { lat: 12.971598, lng: 77.594566 },
  safeZoneRadiusMeters: 180, // Safe operating perimeter
  dangerZoneCenter: { lat: 12.973200, lng: 77.596000 },
  dangerZoneRadiusMeters: 70, // Prohibited Deep Mine Shaft

  // Sensor Telemetry Data
  telemetry: {
    temp: 27.5,
    humidity: 62.0,
    mq2_smoke: 85,     // PPM — MQ-2 Smoke / LPG / Flammable Gas
    mq7_co: 14,        // PPM — MQ-7 Carbon Monoxide
    mq135_air: 78,     // PPM — MQ-135 Air Quality / NH3 / NOx
    mq3_gas: 0.05,     // mg/L — MQ-3 legacy (used by smart_helmet_esp32.ino)
    lat: 12.971598,
    lng: 77.594566,
    alt: 922,
    satellites: 8,
    accelX: 0.05,
    accelY: 0.12,
    accelZ: 0.99,
    accelTotal: 1.01,
    motionState: 'NORMAL', // 'NORMAL', 'INACTIVE', 'FALL'
    alertLevel: 'SAFE'     // 'SAFE', 'WARNING', 'DANGER'
  },

  // Threshold Configurations (match ESP32 firmware values)
  thresholds: {
    tempMax: 40.0,      // °C
    tempWarning: 35.0,
    mq2Danger: 400,     // PPM — MQ-2 Smoke / LPG danger
    mq2Warning: 200,
    mq7Danger: 50,      // PPM — MQ-7 CO (OSHA PEL)
    mq7Warning: 30,
    mq135Danger: 250,   // PPM — MQ-135 toxic air
    mq135Warning: 150,
    mq3Danger: 0.40,    // mg/L — MQ-3 legacy
    mq3Warning: 0.20,
    inactivityTimeout: 60, // seconds
    fallThresholdG: 2.80   // G-Force
  }
};

// Map & Audio handles
let mapInstance    = null;
let workerMarker   = null;
let safeCircle     = null;
let dangerCircle   = null;
let drawControl    = null;
let pendingLayer   = null;          // Polygon awaiting name/type input
let drawnItems     = null;          // L.FeatureGroup for all drawn layers
let audioContext      = null;
let sirenOscillator   = null;
let sirenGain         = null;
let tempAlertTimeout  = null;  // Tracks temperature alert beep cycle
let simulatorInterval = null;
let supabaseClient = null;

// Chart.js Handles & Alert Statistics
let deviceStatusChartInstance = null;
let alertFrequenciesChartInstance = null;
const alertStats = {
  fall: 2,
  geofence: 1,
  suddenMovement: 3
};

// Persisted custom zones: [{ id, name, type, latlngs, layerRef }]
let drawnZones = [];

// ============================================================================
// Initialization
// ============================================================================
document.addEventListener('DOMContentLoaded', () => {
  initTabNavigation();
  initCharts();
  initLeafletMap();
  initLocalStorageConfig();
  setupEventListeners();
  startSimulator();
  updateDashboardUI();
  logIncident('info', 'System online. Monitoring sensors for worker ' + state.activeWorker);

  // Watchdog: refresh online/offline status every 3 seconds
  setInterval(() => {
    updateDashboardUI();
  }, 3000);
});

// ============================================================================
// Leaflet GPS Map & Geofencing Setup (OpenStreetMap)
// ============================================================================
function initLeafletMap() {
  if (typeof L === 'undefined') {
    console.warn('Leaflet library is still loading or unavailable.');
    return;
  }
  const mapElement = document.getElementById('mineMap');
  if (!mapElement || mapInstance) return;

  // Initialize map
  mapInstance = L.map('mineMap', {
    center: [state.mineSiteCenter.lat, state.mineSiteCenter.lng],
    zoom: 17,
    zoomControl: true
  });

  // 1. High-Resolution World Satellite Imagery (Exact basemap from wristb.netlify.app)
  const satelliteBasemap = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
    maxZoom: 19,
    attribution: '&copy; Esri, Maxar, Earthstar Geographics'
  });

  // 2. Reference Roads & Boundaries Overlay (Labels on top of Satellite)
  const referenceOverlay = L.tileLayer('https://services.arcgisonline.com/ArcGIS/rest/services/Reference/World_Boundaries_and_Places/MapServer/tile/{z}/{y}/{x}', {
    maxZoom: 19,
    pane: 'overlayPane',
    opacity: 0.85
  });

  // 3. Official OpenStreetMap (Standard Street Map)
  const osmStandard = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright" target="_blank">OpenStreetMap</a> contributors'
  });

  // 4. Combined Satellite + Labels group as default (wristb.netlify.app style)
  const satelliteHybrid = L.layerGroup([satelliteBasemap, referenceOverlay]);
  satelliteHybrid.addTo(mapInstance);

  // Layer control to toggle between Satellite Hybrid, OpenStreetMap, and Clean Satellite
  const baseMaps = {
    "🛰️ Satellite (Hybrid)": satelliteHybrid,
    "🗺️ OpenStreetMap": osmStandard,
    "📷 Satellite (Imagery Only)": satelliteBasemap
  };
  L.control.layers(baseMaps, null, { position: 'topright', collapsed: true }).addTo(mapInstance);

  // FeatureGroup to hold all Leaflet.draw layers
  drawnItems = new L.FeatureGroup();
  mapInstance.addLayer(drawnItems);

  // Leaflet.draw control (polygon tool only — cleaner for geofencing)
  drawControl = new L.Control.Draw({
    position: 'topright',
    draw: {
      polygon: {
        allowIntersection: false,
        showArea: true,
        shapeOptions: {
          color: '#3B82F6',
          fillColor: '#3B82F6',
          fillOpacity: 0.15,
          weight: 2,
          dashArray: '5, 5'
        },
        metric: true,
        tooltip: {
          start: 'Click to place first vertex',
          cont: 'Click to continue drawing',
          end: 'Double-click to finish zone'
        }
      },
      rectangle: false,
      circle:    false,
      circlemarker: false,
      marker:    false,
      polyline:  false
    },
    edit: { featureGroup: drawnItems }
  });
  // Don't add drawControl to map by default — we control it via button

  // When user finishes drawing a polygon — show the name modal
  mapInstance.on(L.Draw.Event.CREATED, function (e) {
    pendingLayer = e.layer;
    // Preview it on the map temporarily
    drawnItems.addLayer(pendingLayer);
    // Open the zone naming modal
    openZoneModal();
  });

  // After editing existing polygon, update saved latlngs
  mapInstance.on(L.Draw.Event.EDITED, function (e) {
    e.layers.eachLayer(function (layer) {
      const zone = drawnZones.find(z => z.layerRef === layer);
      if (zone) {
        zone.latlngs = layer.getLatLngs()[0].map(ll => ({ lat: ll.lat, lng: ll.lng }));
        saveZonesToStorage();
      }
    });
    logIncident('info', 'Geofence zone boundaries updated.');
  });

  // After deleting polygon(s)
  mapInstance.on(L.Draw.Event.DELETED, function (e) {
    e.layers.eachLayer(function (layer) {
      drawnZones = drawnZones.filter(z => z.layerRef !== layer);
    });
    saveZonesToStorage();
    renderZoneList();
    logIncident('info', 'Geofence zone(s) removed via map editor.');
  });

  // Default static circles
  safeCircle = L.circle([state.mineSiteCenter.lat, state.mineSiteCenter.lng], {
    color: '#10B981',
    fillColor: '#10B981',
    fillOpacity: 0.08,
    weight: 2,
    dashArray: '6, 5',
    radius: state.safeZoneRadiusMeters
  }).addTo(mapInstance)
    .bindPopup('<b style="color:#10B981">Safe Mine Perimeter — Sector A</b><br>Authorized operating area (radius 180m)');

  dangerCircle = L.circle([state.dangerZoneCenter.lat, state.dangerZoneCenter.lng], {
    color: '#EF4444',
    fillColor: '#EF4444',
    fillOpacity: 0.18,
    weight: 2,
    radius: state.dangerZoneRadiusMeters
  }).addTo(mapInstance)
    .bindPopup('<b style="color:#EF4444">RESTRICTED — Deep Mine Shaft</b><br>Toxic / unstable zone. Entry prohibited!');

  // Animated worker helmet marker
  const helmetIcon = L.divIcon({
    className: '',
    html: `
      <div style="position:relative;width:36px;height:36px;display:flex;align-items:center;justify-content:center">
        <div style="position:absolute;width:36px;height:36px;border-radius:50%;background:rgba(245,158,11,0.28);animation:pulse-dot 2s ease-in-out infinite"></div>
        <div style="width:24px;height:24px;border-radius:50%;background:#F59E0B;border:2px solid #fff;display:flex;align-items:center;justify-content:center;box-shadow:0 2px 8px rgba(245,158,11,0.5)">
          <i class="fa-solid fa-helmet-safety" style="color:#0D1117;font-size:11px"></i>
        </div>
      </div>`,
    iconSize:   [36, 36],
    iconAnchor: [18, 18]
  });

  workerMarker = L.marker([state.telemetry.lat, state.telemetry.lng], { icon: helmetIcon })
    .addTo(mapInstance)
    .bindPopup(`<b>Worker ${state.activeWorker}</b><br>Status: Monitoring<br><small>Neo-6M GPS Active</small>`);

  // Load zones saved from previous session
  loadZonesFromStorage();
}

// ============================================================================
// Geofence Calculation (Haversine distance in meters)
// ============================================================================
function calculateDistanceMeters(lat1, lon1, lat2, lon2) {
  const R = 6371e3;
  const φ1 = lat1 * Math.PI / 180;
  const φ2 = lat2 * Math.PI / 180;
  const Δφ = (lat2 - lat1) * Math.PI / 180;
  const Δλ = (lon2 - lon1) * Math.PI / 180;
  const a = Math.sin(Δφ/2)**2 + Math.cos(φ1) * Math.cos(φ2) * Math.sin(Δλ/2)**2;
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
}

/**
 * Ray-casting point-in-polygon check.
 * @param {number} lat
 * @param {number} lng
 * @param {Array<{lat,lng}>} polygon
 */
function pointInPolygon(lat, lng, polygon) {
  let inside = false;
  const n = polygon.length;
  for (let i = 0, j = n - 1; i < n; j = i++) {
    const xi = polygon[i].lat, yi = polygon[i].lng;
    const xj = polygon[j].lat, yj = polygon[j].lng;
    const intersect = ((yi > lng) !== (yj > lng)) &&
      (lat < (xj - xi) * (lng - yi) / (yj - yi) + xi);
    if (intersect) inside = !inside;
  }
  return inside;
}

function evaluateGeofence(lat, lng) {
  const distFromCenter   = calculateDistanceMeters(lat, lng, state.mineSiteCenter.lat, state.mineSiteCenter.lng);
  const distToRestricted = calculateDistanceMeters(lat, lng, state.dangerZoneCenter.lat, state.dangerZoneCenter.lng);

  const kpiGeofenceText = document.getElementById('kpiGeofenceText');
  const kpiDistanceText = document.getElementById('kpiDistanceText');

  // --- Check custom drawn zones first ---
  for (const zone of drawnZones) {
    if (pointInPolygon(lat, lng, zone.latlngs)) {
      const item = document.getElementById('zone-item-' + zone.id);
      if (item) item.classList.add('zone-violated');

      if (zone.type === 'restricted') {
        kpiGeofenceText.textContent = 'ZONE BREACH — ' + zone.name.toUpperCase();
        kpiGeofenceText.className   = 'kpi-value text-danger';
        kpiDistanceText.textContent = 'Inside restricted: ' + zone.name;
        return { status: 'BREACH_RESTRICTED', message: `Worker entered RESTRICTED zone: "${zone.name}"!` };
      } else if (zone.type === 'warning') {
        kpiGeofenceText.textContent = 'CAUTION ZONE — ' + zone.name.toUpperCase();
        kpiGeofenceText.className   = 'kpi-value text-warning';
        kpiDistanceText.textContent = 'Inside caution zone: ' + zone.name;
        return { status: 'WARNING_ZONE', message: `Worker entered caution zone: "${zone.name}"` };
      } else {
        kpiGeofenceText.textContent = 'INSIDE SAFE ZONE';
        kpiGeofenceText.className   = 'kpi-value text-safe';
        kpiDistanceText.textContent = 'Authorized zone: ' + zone.name;
        return { status: 'SAFE', message: '' };
      }
    } else {
      const item = document.getElementById('zone-item-' + zone.id);
      if (item) item.classList.remove('zone-violated');
    }
  }

  // --- Default static zones ---
  if (distToRestricted <= state.dangerZoneRadiusMeters) {
    kpiGeofenceText.textContent = 'RESTRICTED AREA BREACH!';
    kpiGeofenceText.className   = 'kpi-value text-danger';
    kpiDistanceText.textContent = `Inside Toxic Shaft (${Math.round(distToRestricted)}m from epicenter)`;
    return { status: 'BREACH_RESTRICTED', message: 'CRITICAL: Worker entered restricted toxic hazard shaft!' };
  }

  if (distFromCenter > state.safeZoneRadiusMeters) {
    kpiGeofenceText.textContent = 'OUT OF BOUNDS';
    kpiGeofenceText.className   = 'kpi-value text-danger';
    kpiDistanceText.textContent = `${Math.round(distFromCenter - state.safeZoneRadiusMeters)}m outside perimeter`;
    return { status: 'OUT_OF_BOUNDS', message: 'WARNING: Worker strayed outside authorized mine perimeter!' };
  }

  const distToBoundary = Math.max(0, Math.round(state.safeZoneRadiusMeters - distFromCenter));
  kpiGeofenceText.textContent = 'INSIDE SAFE ZONE';
  kpiGeofenceText.className   = 'kpi-value text-safe';
  kpiDistanceText.textContent = `Dist to Perimeter: ~${distToBoundary}m`;
  return { status: 'SAFE', message: '' };
}

// ============================================================================
// UI Updates & Telemetry Evaluation
// ============================================================================
function updateDashboardUI() {
  const d = state.telemetry;

  // 1. Environmental: DHT11
  document.querySelectorAll('[id="tempVal"]').forEach(el => {
    el.textContent = d.temp.toFixed(1);
  });
  document.querySelectorAll('[id="humidVal"]').forEach(el => {
    el.textContent = Math.round(d.humidity);
  });
  
  const tempGauge = document.getElementById('tempGauge');
  const tempBadge = document.getElementById('tempBadge');
  if (d.temp >= state.thresholds.tempMax) {
    tempBadge.textContent = 'Extreme Heat';
    tempBadge.className   = 'badge badge-danger';
    tempGauge.style.borderColor = 'var(--status-danger)';
    tempGauge.style.boxShadow   = '0 0 14px rgba(239,68,68,0.3)';
  } else if (d.temp >= state.thresholds.tempWarning) {
    tempBadge.textContent = 'Warning';
    tempBadge.className   = 'badge badge-warning';
    tempGauge.style.borderColor = 'var(--status-warning)';
    tempGauge.style.boxShadow   = '0 0 14px rgba(245,158,11,0.25)';
  } else {
    tempBadge.textContent = 'Optimal';
    tempBadge.className   = 'badge badge-safe';
    tempGauge.style.borderColor = 'var(--status-info)';
    tempGauge.style.boxShadow   = 'none';
  }

  // 2. Gas Array: MQ-7 (Carbon Monoxide)
  document.getElementById('mq7Val').textContent = Math.round(d.mq7_co);
  const mq7Progress = document.getElementById('mq7Progress');
  const mq7Badge = document.getElementById('mq7Badge');
  const mq7Percent = Math.min(100, (d.mq7_co / 100) * 100);
  mq7Progress.style.width = mq7Percent + '%';

  if (d.mq7_co >= state.thresholds.mq7Danger) {
    mq7Badge.textContent = 'LETHAL CO LEVEL';
    mq7Badge.className = 'badge badge-danger';
    mq7Progress.className = 'progress-fill fill-danger';
  } else if (d.mq7_co >= state.thresholds.mq7Warning) {
    mq7Badge.textContent = 'Elevated CO';
    mq7Badge.className = 'badge badge-warning';
    mq7Progress.className = 'progress-fill fill-warning';
  } else {
    mq7Badge.textContent = 'Safe';
    mq7Badge.className = 'badge badge-safe';
    mq7Progress.className = 'progress-fill fill-safe';
  }

  // 3. Gas Array: MQ-135 (Toxic gases: NH3, Smoke, Benzene)
  document.getElementById('mq135Val').textContent = Math.round(d.mq135_air);
  const mq135Progress = document.getElementById('mq135Progress');
  const mq135Badge = document.getElementById('mq135Badge');
  const mq135Percent = Math.min(100, (d.mq135_air / 400) * 100);
  mq135Progress.style.width = mq135Percent + '%';

  if (d.mq135_air >= state.thresholds.mq135Danger) {
    mq135Badge.textContent = 'HAZARDOUS AIR';
    mq135Badge.className = 'badge badge-danger';
    mq135Progress.className = 'progress-fill fill-danger';
  } else if (d.mq135_air >= state.thresholds.mq135Warning) {
    mq135Badge.textContent = 'Moderate Quality';
    mq135Badge.className = 'badge badge-warning';
    mq135Progress.className = 'progress-fill fill-warning';
  } else {
    mq135Badge.textContent = 'Good Quality';
    mq135Badge.className = 'badge badge-safe';
    mq135Progress.className = 'progress-fill fill-safe';
  }

  // 4. Gas Array: MQ-2 (Smoke / LPG / Flammable Gas — PPM)
  document.getElementById('mq3Val').textContent = Math.round(d.mq2_smoke);
  const mq3Progress = document.getElementById('mq3Progress');
  const mq3Badge = document.getElementById('mq3Badge');
  const mq3Percent = Math.min(100, (d.mq2_smoke / 800) * 100);  // 800 PPM = full scale
  mq3Progress.style.width = mq3Percent + '%';

  if (d.mq2_smoke >= state.thresholds.mq2Danger) {
    mq3Badge.textContent = 'SMOKE / FIRE RISK!';
    mq3Badge.className = 'badge badge-danger';
    mq3Progress.className = 'progress-fill fill-danger';
  } else if (d.mq2_smoke >= state.thresholds.mq2Warning) {
    mq3Badge.textContent = 'Smoke Detected';
    mq3Badge.className = 'badge badge-warning';
    mq3Progress.className = 'progress-fill fill-warning';
  } else {
    mq3Badge.textContent = 'Safe';
    mq3Badge.className = 'badge badge-safe';
    mq3Progress.className = 'progress-fill fill-safe';
  }

  // 5. Motion: MPU-6050
  document.getElementById('accelTotalVal').textContent = d.accelTotal.toFixed(2) + ' G';
  document.getElementById('axVal').textContent = d.accelX.toFixed(2);
  document.getElementById('ayVal').textContent = d.accelY.toFixed(2);
  document.getElementById('azVal').textContent = d.accelZ.toFixed(2);
  document.getElementById('axBar').style.width = Math.min(100, Math.abs(d.accelX) * 50 + 50) + '%';
  document.getElementById('ayBar').style.width = Math.min(100, Math.abs(d.accelY) * 50 + 50) + '%';
  document.getElementById('azBar').style.width = Math.min(100, Math.abs(d.accelZ) * 80) + '%';

  const kpiMotionText = document.getElementById('kpiMotionText');
  const kpiMotionIcon = document.getElementById('kpiMotionIcon');
  const kpiInactivityText = document.getElementById('kpiInactivityText');
  const inactivityBox = document.getElementById('inactivityBox');
  const inactivityNotice = document.getElementById('inactivityNotice');
  const inactivityTimerDisplay = document.getElementById('inactivityTimerDisplay');

  inactivityTimerDisplay.textContent = state.inactivitySeconds + 's';
  kpiInactivityText.textContent = `Inactivity: ${state.inactivitySeconds}s / ${state.thresholds.inactivityTimeout}s threshold`;

  if (state.isFallDetected) {
    kpiMotionText.textContent = 'FALL IMPACT DETECTED!';
    kpiMotionText.className = 'kpi-value text-danger';
    kpiMotionIcon.className = 'fa-solid fa-person-falling-burst';
    inactivityBox.className = 'inactivity-status-box danger-alert';
    inactivityNotice.textContent = 'EMERGENCY: Sudden high-G impact & fall detected!';
  } else if (state.inactivitySeconds >= state.thresholds.inactivityTimeout) {
    kpiMotionText.textContent = 'PROLONGED INACTIVITY!';
    kpiMotionText.className = 'kpi-value text-danger';
    kpiMotionIcon.className = 'fa-solid fa-bed-pulse';
    inactivityBox.className = 'inactivity-status-box danger-alert';
    inactivityNotice.textContent = `MAN DOWN ALARM: Worker motionless for >${state.thresholds.inactivityTimeout}s!`;
  } else {
    kpiMotionText.textContent = 'ACTIVE (MOVING)';
    kpiMotionText.className = 'kpi-value text-safe';
    kpiMotionIcon.className = 'fa-solid fa-person-walking';
    inactivityBox.className = 'inactivity-status-box';
    inactivityNotice.textContent = 'Normal worker movement detected.';
  }

  // 6. Map & GPS Coordinates
  document.getElementById('workerLat').textContent = d.lat.toFixed(6);
  document.getElementById('workerLng').textContent = d.lng.toFixed(6);
  document.getElementById('workerAlt').textContent = Math.round(d.alt) + ' m';
  document.getElementById('workerSats').textContent = d.satellites;

  if (workerMarker) {
    workerMarker.setLatLng([d.lat, d.lng]);
  }

  // 7. Geofence Evaluation
  const geofenceResult = evaluateGeofence(d.lat, d.lng);

  // 8. Aggregate Hazard Evaluation & Alerts
  evaluateOverallSafety(geofenceResult);
}

// ============================================================================
// Overall Safety & Alert Engine
// ============================================================================
function evaluateOverallSafety(geofenceResult) {
  const d = state.telemetry;
  let hazards = [];

  if (d.mq7_co >= state.thresholds.mq7Danger) {
    hazards.push(`Lethal Carbon Monoxide: ${Math.round(d.mq7_co)} PPM`);
  }
  if (d.mq135_air >= state.thresholds.mq135Danger) {
    hazards.push(`Toxic Gas Breach: ${Math.round(d.mq135_air)} PPM`);
  }
  // MQ-2: Smoke / LPG / Flammable Gas
  if (d.mq2_smoke >= state.thresholds.mq2Danger) {
    hazards.push(`🔥 Smoke / Flammable Gas: ${Math.round(d.mq2_smoke)} PPM (MQ-2)`);
  } else if (d.mq2_smoke >= state.thresholds.mq2Warning) {
    hazards.push(`Smoke Traces Detected: ${Math.round(d.mq2_smoke)} PPM (MQ-2)`);
  }
  if (d.temp >= state.thresholds.tempMax) {
    hazards.push(`Critical High Heat: ${d.temp.toFixed(1)}\u00b0C`);
    // Fire distinct temperature alert sound (non-blocking, plays once per trigger)
    playTemperatureAlertSound();
  }
  if (geofenceResult.status === 'BREACH_RESTRICTED') {
    hazards.push(`Restricted Hazard Chamber Entered!`);
  } else if (geofenceResult.status === 'OUT_OF_BOUNDS') {
    hazards.push(`Worker Outside Safe Mine Perimeter!`);
  }
  if (state.isFallDetected) {
    hazards.push(`Severe Fall Impact Shock Detected!`);
  }
  if (state.inactivitySeconds >= state.thresholds.inactivityTimeout) {
    hazards.push(`Unresponsive Worker: No movement for >60s`);
  }

  const kpiOverallCard = document.getElementById('kpiOverallCard');
  const kpiStatusIconWrap = document.getElementById('kpiStatusIconWrap');
  const kpiStatusIcon = document.getElementById('kpiStatusIcon');
  const kpiStatusText = document.getElementById('kpiStatusText');
  const kpiGasRiskText = document.getElementById('kpiGasRiskText');
  const kpiGasSummary = document.getElementById('kpiGasSummary');
  const emergencyBanner = document.getElementById('emergencyBanner');
  const emergencyTitle = document.getElementById('emergencyTitle');
  const emergencyDetail = document.getElementById('emergencyDetail');

  if (hazards.length > 0) {
    // CRITICAL DANGER
    kpiStatusText.textContent = 'DANGER / EMERGENCY';
    kpiStatusText.className = 'kpi-value text-danger';
    kpiStatusIconWrap.className = 'kpi-icon-wrap icon-danger';
    kpiStatusIcon.className = 'fa-solid fa-triangle-exclamation';

    kpiGasRiskText.textContent = 'CRITICAL RISK';
    kpiGasRiskText.className = 'kpi-value text-danger';
    kpiGasSummary.innerHTML = `<span style="color:#fca5a5;font-weight:600">${hazards[0]}</span><br><span style="opacity:0.85;font-size:0.75rem">MQ-2: ${Math.round(d.mq2_smoke)} PPM &bull; MQ-7: ${Math.round(d.mq7_co)} PPM &bull; MQ-135: ${Math.round(d.mq135_air)} PPM</span>`;

    // Show Emergency Banner
    emergencyBanner.classList.remove('hidden');
    emergencyTitle.textContent = `EMERGENCY ALERT: ${state.activeWorker}`;
    emergencyDetail.textContent = hazards.join(' | ');

    triggerAudioAlarm(true);
  } else {
    // SAFE STATE
    kpiStatusText.textContent = 'NORMAL / SAFE';
    kpiStatusText.className = 'kpi-value text-safe';
    kpiStatusIconWrap.className = 'kpi-icon-wrap status-safe';
    kpiStatusIcon.className = 'fa-solid fa-shield-heart';

    kpiGasRiskText.textContent = 'LOW HAZARD';
    kpiGasRiskText.className = 'kpi-value text-safe';
    kpiGasSummary.textContent = `MQ-2: ${Math.round(d.mq2_smoke)} PPM  |  MQ-7: ${Math.round(d.mq7_co)} PPM  |  MQ-135: ${Math.round(d.mq135_air)} PPM`;

    emergencyBanner.classList.add('hidden');
    triggerAudioAlarm(false);
  }

  // Update Top 5 System Overview Metric Cards
  const kpiTotalDevices = document.getElementById('kpiTotalDevices');
  const kpiOnlineDevices = document.getElementById('kpiOnlineDevices');
  const kpiOfflineDevices = document.getElementById('kpiOfflineDevices');
  const kpiActiveAlerts = document.getElementById('kpiActiveAlerts');
  const kpiActiveZones = document.getElementById('kpiActiveZones');
  const snapshotWorkerId = document.getElementById('snapshotWorkerId');
  const navAlertBadge = document.getElementById('navAlertBadge');

  // Dynamic Online/Offline Detection:
  // In simulator mode: online.
  // In Supabase mode: online if packet received within last 20 seconds.
  const isOnline = state.dataSource === 'sim' 
    ? true 
    : Boolean(state.lastTelemetryTimestamp && (Date.now() - state.lastTelemetryTimestamp < 20000));
  state.isDeviceOnline = isOnline;

  if (kpiTotalDevices) kpiTotalDevices.textContent = '1';
  if (kpiOnlineDevices) kpiOnlineDevices.textContent = isOnline ? '1' : '0';
  if (kpiOfflineDevices) kpiOfflineDevices.textContent = isOnline ? '0' : '1';

  // Dynamic Donut Chart update (matches live Online/Offline count)
  if (deviceStatusChartInstance && deviceStatusChartInstance.data && deviceStatusChartInstance.data.datasets) {
    deviceStatusChartInstance.data.datasets[0].data = isOnline ? [1, 0] : [0, 1];
    deviceStatusChartInstance.data.datasets[0].backgroundColor = isOnline ? ['#10B981', '#334155'] : ['#334155', '#EF4444'];
    deviceStatusChartInstance.update('none');
  }

  if (kpiActiveAlerts) kpiActiveAlerts.textContent = hazards.length.toString();
  if (kpiActiveZones) kpiActiveZones.textContent = (drawnZones.length).toString();
  if (snapshotWorkerId) snapshotWorkerId.textContent = state.activeWorker;

  if (navAlertBadge) {
    if (hazards.length > 0) {
      navAlertBadge.textContent = hazards.length.toString();
      navAlertBadge.classList.remove('hidden');
    } else {
      navAlertBadge.classList.add('hidden');
    }
  }

  const now = new Date();
  const lastSyncEl = document.getElementById('lastUpdatedTime');
  if (lastSyncEl) lastSyncEl.textContent = `Last sync: ${now.toLocaleTimeString()}`;
}

// ============================================================================
// Temperature-Specific Alert Sound
// A distinct rising-pitch double-chirp tone so the operator instantly knows
// the hazard is HEAT — different from the continuous siren for gas/fall.
// ============================================================================
function playTemperatureAlertSound() {
  if (state.sirenMuted) return;
  try {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) return;
    if (!audioContext) audioContext = new AudioCtx();
    if (audioContext.state === 'suspended') audioContext.resume();

    const now = audioContext.currentTime;
    const gainNode = audioContext.createGain();
    gainNode.gain.setValueAtTime(0, now);
    gainNode.connect(audioContext.destination);

    // Two rising-chirp tones — distinctive "heat" pattern
    [0, 0.35].forEach((startOffset, i) => {
      const osc = audioContext.createOscillator();
      osc.type = 'triangle';
      // Chirp: sweep from 880 Hz → 1760 Hz over 200 ms
      osc.frequency.setValueAtTime(880, now + startOffset);
      osc.frequency.linearRampToValueAtTime(1760, now + startOffset + 0.18);
      osc.connect(gainNode);
      osc.start(now + startOffset);
      osc.stop(now + startOffset + 0.20);
    });

    // Volume envelope: fade in → sustain → fade out
    gainNode.gain.setValueAtTime(0,    now);
    gainNode.gain.linearRampToValueAtTime(0.25, now + 0.02);
    gainNode.gain.setValueAtTime(0.25, now + 0.50);
    gainNode.gain.linearRampToValueAtTime(0,    now + 0.60);
  } catch (err) {
    console.warn('[TempAlert] Audio error:', err);
  }
}

// ============================================================================
// Web Audio API Siren / Alarm  (used for all non-temperature hazards)
// ============================================================================
function triggerAudioAlarm(enable) {
  if (state.sirenMuted) {
    stopSirenSound();
    return;
  }

  if (enable) {
    startSirenSound();
    document.getElementById('sirenMuteBtn').classList.add('active-siren');
  } else {
    stopSirenSound();
    document.getElementById('sirenMuteBtn').classList.remove('active-siren');
  }
}

function startSirenSound() {
  if (sirenOscillator) return; // Already sounding

  try {
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    if (!AudioContext) return;
    if (!audioContext) audioContext = new AudioContext();

    if (audioContext.state === 'suspended') {
      audioContext.resume();
    }

    sirenOscillator = audioContext.createOscillator();
    sirenGain = audioContext.createGain();

    sirenOscillator.type = 'sawtooth';
    sirenOscillator.frequency.setValueAtTime(650, audioContext.currentTime);

    // Modulate pitch up and down (Industrial Siren effect)
    const lfo = audioContext.createOscillator();
    const lfoGain = audioContext.createGain();
    lfo.type = 'sine';
    lfo.frequency.setValueAtTime(2.5, audioContext.currentTime); // 2.5 Hz siren cycle
    lfoGain.gain.setValueAtTime(250, audioContext.currentTime);

    lfo.connect(sirenOscillator.frequency);
    lfo.start();

    sirenGain.gain.setValueAtTime(0.2, audioContext.currentTime);
    sirenOscillator.connect(sirenGain);
    sirenGain.connect(audioContext.destination);

    sirenOscillator.start();
  } catch (err) {
    console.warn('Audio context unavailable:', err);
  }
}

function stopSirenSound() {
  if (sirenOscillator) {
    try {
      sirenOscillator.stop();
      sirenOscillator.disconnect();
    } catch (e) {}
    sirenOscillator = null;
  }
}

// ============================================================================
// Real-time Event Logger
// ============================================================================
function logIncident(type, message) {
  const container = document.getElementById('alertLogsContainer');
  if (!container) return;

  const now = new Date();
  const timeStr = now.toLocaleTimeString();

  const entry = document.createElement('div');
  entry.className = `log-entry log-entry-${type}`;
  entry.innerHTML = `
    <span class="log-time">${timeStr}</span>
    <span class="log-msg">${message}</span>
  `;

  container.insertBefore(entry, container.firstChild);

  // Limit to 40 log items
  while (container.children.length > 40) {
    container.removeChild(container.lastChild);
  }
}

// ============================================================================
// Built-in Realistic Hardware Simulation Engine
// ============================================================================
function startSimulator() {
  if (simulatorInterval) clearInterval(simulatorInterval);

  simulatorInterval = setInterval(() => {
    if (state.dataSource !== 'sim') return;

    // Subtle natural sensor jitter
    state.telemetry.temp += (Math.random() - 0.5) * 0.15;
    state.telemetry.humidity += (Math.random() - 0.5) * 0.2;
    state.telemetry.mq7_co    += (Math.random() - 0.5) * 0.8;
    state.telemetry.mq135_air += (Math.random() - 0.5) * 1.5;
    state.telemetry.mq2_smoke += (Math.random() - 0.5) * 3.0;   // MQ-2 PPM jitter
    state.telemetry.mq3_gas   += (Math.random() - 0.5) * 0.005; // MQ-3 legacy

    // Bounds safety clamp
    state.telemetry.temp      = Math.max(20, Math.min(50, state.telemetry.temp));
    state.telemetry.mq7_co    = Math.max(5,  state.telemetry.mq7_co);
    state.telemetry.mq135_air = Math.max(40, state.telemetry.mq135_air);
    state.telemetry.mq2_smoke = Math.max(10, state.telemetry.mq2_smoke);  // min 10 PPM
    state.telemetry.mq3_gas   = Math.max(0.01, state.telemetry.mq3_gas);

    // Worker walking simulation (slight wander)
    if (!state.isFallDetected && state.inactivitySeconds < state.thresholds.inactivityTimeout) {
      state.telemetry.lat += (Math.random() - 0.5) * 0.00004;
      state.telemetry.lng += (Math.random() - 0.5) * 0.00004;

      // IMU walking fluctuation
      state.telemetry.accelX = (Math.random() - 0.5) * 0.3;
      state.telemetry.accelY = (Math.random() - 0.5) * 0.3;
      state.telemetry.accelZ = 0.95 + (Math.random() - 0.5) * 0.15;
      state.telemetry.accelTotal = Math.sqrt(
        state.telemetry.accelX ** 2 + state.telemetry.accelY ** 2 + state.telemetry.accelZ ** 2
      );

      // Inactivity resets when walking
      state.inactivitySeconds = Math.max(0, state.inactivitySeconds - 1);
    } else {
      // Inactivity accumulation
      state.inactivitySeconds += 2;
      state.telemetry.accelX = 0.01;
      state.telemetry.accelY = 0.01;
      state.telemetry.accelZ = 1.00;
      state.telemetry.accelTotal = 1.00;
    }

    updateDashboardUI();
  }, 2000);
}

// ============================================================================
// Event Listeners & Interactive Handlers
// ============================================================================
function setupEventListeners() {
  // Worker select
  document.getElementById('workerSelect').addEventListener('change', (e) => {
    state.activeWorker = e.target.value;
    logIncident('info', `Switched monitoring view to ${e.target.value}`);
    updateDashboardUI();
  });

  // Mute Siren
  const muteBtn = document.getElementById('sirenMuteBtn');
  const sirenIcon = document.getElementById('sirenIcon');
  muteBtn.addEventListener('click', () => {
    state.sirenMuted = !state.sirenMuted;
    if (state.sirenMuted) {
      stopSirenSound();
      sirenIcon.className = 'fa-solid fa-volume-xmark';
      muteBtn.classList.remove('active-siren');
      logIncident('warning', 'Audible hazard siren muted by supervisor.');
    } else {
      sirenIcon.className = 'fa-solid fa-volume-high';
      logIncident('info', 'Audible hazard siren enabled.');
      updateDashboardUI();
    }
  });

  // Recenter Map
  document.getElementById('recenterMapBtn').addEventListener('click', () => {
    if (mapInstance && workerMarker) {
      mapInstance.setView(workerMarker.getLatLng(), 17, { animate: true });
    }
  });

  // Test Geofence Breach (Moves worker into restricted chamber)
  document.getElementById('triggerGeofenceBreachSimBtn').addEventListener('click', () => {
    state.telemetry.lat = state.dangerZoneCenter.lat;
    state.telemetry.lng = state.dangerZoneCenter.lng;
    logIncident('danger', 'GEOFENCE ALERT: Worker has entered Restricted Hazard Shaft!');
    alertStats.geofence++;
    updateAlertFrequenciesChart();
    updateDashboardUI();
    if (mapInstance) {
      mapInstance.setView([state.telemetry.lat, state.telemetry.lng], 18, { animate: true });
    }
  });

  // Simulator Triggers
  document.getElementById('simToxicGasBtn').addEventListener('click', () => {
    state.telemetry.mq7_co = 72; // Above 50 PPM danger
    state.telemetry.mq135_air = 310; // Above 250 PPM danger
    logIncident('danger', 'SIMULATION TRIGGER: Carbon Monoxide (72 PPM) and Ammonia gas leak!');
    alertStats.suddenMovement++;
    updateAlertFrequenciesChart();
    updateDashboardUI();
  });

  document.getElementById('simHighTempBtn').addEventListener('click', () => {
    state.telemetry.temp = 41.0; // Above 40°C danger threshold
    state.telemetry.humidity = 88;
    logIncident('danger', 'SIMULATION TRIGGER: Mine shaft extreme heat wave (41.0°C)! Temperature exceeds 40°C safety limit.');
    alertStats.suddenMovement++;
    updateAlertFrequenciesChart();
    updateDashboardUI();
  });

  document.getElementById('simFallBtn').addEventListener('click', () => {
    state.isFallDetected = true;
    state.telemetry.accelTotal = 3.65; // High impact shock (> 2.80G)
    logIncident('danger', 'SIMULATION TRIGGER: 3.65G impact registered. Worker fall detected!');
    alertStats.fall++;
    updateAlertFrequenciesChart();
    updateDashboardUI();
  });

  document.getElementById('simInactivityBtn').addEventListener('click', () => {
    state.inactivitySeconds = 65; // Above 60s timeout
    logIncident('danger', 'SIMULATION TRIGGER: Worker inactive and unresponsive for 65 seconds!');
    alertStats.suddenMovement++;
    updateAlertFrequenciesChart();
    updateDashboardUI();
  });

  document.getElementById('simResetNormalBtn').addEventListener('click', () => {
    state.telemetry.temp = 27.5;
    state.telemetry.humidity = 62;
    state.telemetry.mq7_co    = 14;
    state.telemetry.mq135_air = 78;
    state.telemetry.mq2_smoke = 85;   // MQ-2 reset
    state.telemetry.mq3_gas   = 0.05; // MQ-3 legacy reset
    state.telemetry.lat = state.mineSiteCenter.lat;
    state.telemetry.lng = state.mineSiteCenter.lng;
    state.telemetry.accelTotal = 1.01;
    state.isFallDetected = false;
    state.inactivitySeconds = 0;
    logIncident('safe', 'All telemetry parameters reset to SAFE nominal values.');
    updateDashboardUI();
    if (mapInstance) {
      mapInstance.setView([state.mineSiteCenter.lat, state.mineSiteCenter.lng], 17);
    }
  });

  // Dismiss Emergency Banner
  document.getElementById('dismissAlertBtn').addEventListener('click', () => {
    document.getElementById('emergencyBanner').classList.add('hidden');
    stopSirenSound();
    logIncident('info', 'Hazard alert acknowledged by operator.');
  });

  // Clear Logs
  document.getElementById('clearLogsBtn').addEventListener('click', () => {
    document.getElementById('alertLogsContainer').innerHTML = '';
  });

  // Supabase Config Modal
  const modal = document.getElementById('configModal');
  document.getElementById('openConfigBtn').addEventListener('click', () => {
    modal.classList.remove('hidden');
  });
  document.getElementById('closeConfigBtn').addEventListener('click', () => {
    modal.classList.add('hidden');
  });
  document.getElementById('saveConfigBtn').addEventListener('click', () => {
    saveSupabaseConfig();
  });

  // ── Draw Zone button ─────────────────────────────────────────────────────
  const drawBtn = document.getElementById('startDrawPolygonBtn');
  drawBtn.addEventListener('click', () => {
    if (!mapInstance) return;
    const isActive = drawBtn.classList.contains('drawing-active');
    if (isActive) {
      // Cancel draw mode
      mapInstance.removeControl(drawControl);
      drawBtn.classList.remove('drawing-active');
      drawBtn.innerHTML = '<i class="fa-solid fa-draw-polygon"></i> Draw Zone';
    } else {
      // Activate draw mode — add control + auto-start polygon tool
      mapInstance.addControl(drawControl);
      drawBtn.classList.add('drawing-active');
      drawBtn.innerHTML = '<i class="fa-solid fa-xmark"></i> Cancel Draw';
      // Programmatically trigger the polygon draw handler
      new L.Draw.Polygon(mapInstance, drawControl.options.draw.polygon).enable();
    }
  });

  // ── Zone create modal events ─────────────────────────────────────────────
  const zoneModal    = document.getElementById('zoneCreateModal');
  const zoneTypeSelect = document.getElementById('zoneTypeSelect');
  const zoneTypeDesc   = document.getElementById('zoneTypeDesc');

  const typeDescMap = {
    restricted: { cls: '', icon: 'fa-circle-exclamation', color: 'var(--status-danger)', text: 'Workers entering this zone will trigger an immediate EMERGENCY alert.' },
    warning:    { cls: 'type-warning', icon: 'fa-triangle-exclamation', color: 'var(--status-warning)', text: 'Workers entering this zone will trigger a CAUTION warning.' },
    safe:       { cls: 'type-safe',    icon: 'fa-shield-check', color: 'var(--status-safe)', text: 'This is an authorized zone — no alert is triggered on entry.' }
  };

  zoneTypeSelect.addEventListener('change', () => {
    const info = typeDescMap[zoneTypeSelect.value];
    zoneTypeDesc.className = 'zone-type-desc ' + info.cls;
    zoneTypeDesc.innerHTML = `<i class="fa-solid ${info.icon}" style="color:${info.color};flex-shrink:0"></i> ${info.text}`;
  });

  document.getElementById('cancelZoneBtn').addEventListener('click', () => {
    discardPendingZone();
  });
  document.getElementById('discardZoneBtn').addEventListener('click', () => {
    discardPendingZone();
  });
  document.getElementById('saveZoneBtn').addEventListener('click', () => {
    saveNewZone();
  });

  // ── Default circle toggles ───────────────────────────────────────────────
  document.getElementById('toggleSafeCircle').addEventListener('change', function () {
    if (!mapInstance) return;
    this.checked ? safeCircle.addTo(mapInstance) : mapInstance.removeLayer(safeCircle);
  });
  document.getElementById('toggleDangerCircle').addEventListener('change', function () {
    if (!mapInstance) return;
    this.checked ? dangerCircle.addTo(mapInstance) : mapInstance.removeLayer(dangerCircle);
  });
}

// ============================================================================
// Supabase Integration Handlers
// ============================================================================
function initLocalStorageConfig() {
  const defaultUrl = 'https://srkowkuclkimtwhkiutg.supabase.co';
  const defaultKey = 'sb_publishable_kWHQSFzZEblEqWzlTwZuvg_ISGBBUCu';

  const savedUrl = localStorage.getItem('mineguard_supabase_url') || defaultUrl;
  const savedKey = localStorage.getItem('mineguard_supabase_key') || defaultKey;
  const savedSource = localStorage.getItem('mineguard_data_source') || 'supabase';

  document.getElementById('supabaseUrlInput').value = savedUrl;
  document.getElementById('supabaseAnonKeyInput').value = savedKey;
  document.getElementById('dataSourceSelect').value = savedSource;

  state.dataSource = savedSource;
  if (savedSource === 'supabase' && savedUrl && savedKey) {
    initSupabaseConnection(savedUrl, savedKey);
  }
}

function saveSupabaseConfig() {
  const url = document.getElementById('supabaseUrlInput').value.trim();
  const key = document.getElementById('supabaseAnonKeyInput').value.trim();
  const source = document.getElementById('dataSourceSelect').value;

  localStorage.setItem('mineguard_supabase_url', url);
  localStorage.setItem('mineguard_supabase_key', key);
  localStorage.setItem('mineguard_data_source', source);

  state.dataSource = source;
  document.getElementById('configModal').classList.add('hidden');

  if (source === 'supabase') {
    if (!url || !key) {
      alert('Please enter both Supabase Project URL and Anon Key!');
      return;
    }
    initSupabaseConnection(url, key);
  } else {
    document.getElementById('connectionStatusText').textContent = 'SIMULATOR ACTIVE';
    logIncident('info', 'Switched to local Demo Simulator mode.');
  }
}

let supabasePollingTimer = null;

async function fetchLatestSupabaseTelemetry() {
  if (!supabaseClient || state.dataSource !== 'supabase') return;
  try {
    const { data, error } = await supabaseClient
      .from('helmet_telemetry')
      .select('*')
      .order('id', { ascending: false })
      .limit(1);

    if (error) {
      console.warn('[Supabase Fetch] Error:', error.message);
      return;
    }

    if (data && data.length > 0) {
      const row = data[0];
      if (row.id !== state.lastTelemetryId) {
        state.lastTelemetryId = row.id;
        receiveHardwareTelemetry(row);
      }
    }
  } catch (err) {
    console.warn('[Supabase Fetch] Exception:', err);
  }
}

function initSupabaseConnection(url, key) {
  if (!window.supabase) {
    console.error('Supabase client library not loaded');
    return;
  }

  if (supabasePollingTimer) {
    clearInterval(supabasePollingTimer);
    supabasePollingTimer = null;
  }

  try {
    supabaseClient = window.supabase.createClient(url, key);
    document.getElementById('connectionStatusText').textContent = 'CONNECTING SUPABASE...';

    // 1. Immediate fetch on connection so dashboard has latest state without waiting
    fetchLatestSupabaseTelemetry();

    // 2. Subscribe to Realtime inserts on 'helmet_telemetry' table
    supabaseClient
      .channel('public:helmet_telemetry')
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'helmet_telemetry' }, (payload) => {
        const row = payload.new;
        if (row && (row.worker_id === state.activeWorker || !row.worker_id)) {
          state.lastTelemetryId = row.id;
          receiveHardwareTelemetry(row);
        }
      })
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') {
          document.getElementById('connectionStatusText').textContent = 'SUPABASE CLOUD LIVE';
          logIncident('safe', 'Connected to Supabase Realtime channel.');
          fetchLatestSupabaseTelemetry();
        } else if (status === 'CLOSED' || status === 'CHANNEL_ERROR') {
          document.getElementById('connectionStatusText').textContent = 'SUPABASE OFFLINE';
        }
      });

    // 3. Resilient Polling Fallback (every 2.5 seconds)
    // Ensures telemetry updates smoothly even if WebSockets are blocked or Realtime replication isn't configured in Postgres
    supabasePollingTimer = setInterval(() => {
      fetchLatestSupabaseTelemetry();
    }, 2500);

  } catch (err) {
    console.error('Failed to init Supabase:', err);
    logIncident('warning', 'Failed to connect to Supabase: ' + err.message);
  }
}

function receiveHardwareTelemetry(row) {
  // Update heartbeat for live Online/Offline status
  state.lastTelemetryTimestamp = Date.now();
  state.isDeviceOnline = true;

  if (row.temperature != null) {
    const newTemp = parseFloat(row.temperature);
    if (!isNaN(newTemp)) {
      if (newTemp >= state.thresholds.tempMax && state.telemetry.temp < state.thresholds.tempMax) {
        logIncident('danger',
          `🔥 HEAT ALERT: Temperature rose to ${newTemp.toFixed(1)}°C — exceeds ${state.thresholds.tempMax}°C safety limit! Evacuate worker immediately.`);
        playTemperatureAlertSound();
      } else if (newTemp < state.thresholds.tempMax && state.telemetry.temp >= state.thresholds.tempMax) {
        logIncident('safe', `Temperature dropped to ${newTemp.toFixed(1)}°C — back within safe range.`);
      }
      state.telemetry.temp = newTemp;
    }
  }

  if (row.humidity != null && !isNaN(parseFloat(row.humidity))) {
    state.telemetry.humidity = parseFloat(row.humidity);
  }
  if (row.mq7_co != null && !isNaN(parseFloat(row.mq7_co))) {
    state.telemetry.mq7_co = parseFloat(row.mq7_co);
  }
  if (row.mq135_air != null && !isNaN(parseFloat(row.mq135_air))) {
    state.telemetry.mq135_air = parseFloat(row.mq135_air);
  }

  // MQ-2 (Smoke / LPG / Flammable Gas):
  // Check if dedicated mq2_smoke column exists; if null/undefined, fall back to mq3_gas where earlier firmware sent it
  if (row.mq2_smoke != null && !isNaN(parseFloat(row.mq2_smoke))) {
    state.telemetry.mq2_smoke = parseFloat(row.mq2_smoke);
  } else if (row.mq3_gas != null && !isNaN(parseFloat(row.mq3_gas))) {
    state.telemetry.mq2_smoke = parseFloat(row.mq3_gas);
  }

  if (row.mq3_gas != null && !isNaN(parseFloat(row.mq3_gas))) {
    state.telemetry.mq3_gas = parseFloat(row.mq3_gas);
  }

  if (row.latitude != null && !isNaN(parseFloat(row.latitude))) state.telemetry.lat = parseFloat(row.latitude);
  if (row.longitude != null && !isNaN(parseFloat(row.longitude))) state.telemetry.lng = parseFloat(row.longitude);
  if (row.accel_total != null && !isNaN(parseFloat(row.accel_total))) state.telemetry.accelTotal = parseFloat(row.accel_total);
  if (row.is_fall != null) state.isFallDetected = Boolean(row.is_fall);
  if (row.inactivity_secs != null && !isNaN(parseInt(row.inactivity_secs))) state.inactivitySeconds = parseInt(row.inactivity_secs);

  logIncident('info', `Hardware payload received from ESP32 (${state.activeWorker}).`);
  updateDashboardUI();
}

// ============================================================================
// Geofence Zone Management
// ============================================================================

/** Open zone naming modal after drawing completes */
function openZoneModal() {
  // Reset draw button state
  const drawBtn = document.getElementById('startDrawPolygonBtn');
  if (drawBtn) {
    drawBtn.classList.remove('drawing-active');
    drawBtn.innerHTML = '<i class="fa-solid fa-draw-polygon"></i> Draw Zone';
    if (mapInstance) mapInstance.removeControl(drawControl);
  }
  // Reset form
  document.getElementById('zoneNameInput').value = '';
  document.getElementById('zoneTypeSelect').value = 'restricted';
  const desc = document.getElementById('zoneTypeDesc');
  desc.className = 'zone-type-desc';
  desc.innerHTML = `<i class="fa-solid fa-circle-exclamation" style="color:var(--status-danger);flex-shrink:0"></i> Workers entering this zone will trigger an immediate EMERGENCY alert.`;
  // Show modal
  document.getElementById('zoneCreateModal').classList.remove('hidden');
}

/** Discard the pending drawn polygon */
function discardPendingZone() {
  if (pendingLayer && drawnItems) {
    drawnItems.removeLayer(pendingLayer);
  }
  pendingLayer = null;
  document.getElementById('zoneCreateModal').classList.add('hidden');
  logIncident('info', 'Zone drawing discarded.');
}

/** Save the pending polygon as a named zone */
function saveNewZone() {
  if (!pendingLayer) return;

  const name = document.getElementById('zoneNameInput').value.trim() || 'Unnamed Zone';
  const type = document.getElementById('zoneTypeSelect').value;

  // Style based on type
  const styles = {
    restricted: { color: '#EF4444', fillColor: '#EF4444', fillOpacity: 0.18, dashArray: null },
    warning:    { color: '#F59E0B', fillColor: '#F59E0B', fillOpacity: 0.14, dashArray: '6,4' },
    safe:       { color: '#10B981', fillColor: '#10B981', fillOpacity: 0.10, dashArray: '6,4' }
  };
  const s = styles[type] || styles.restricted;

  pendingLayer.setStyle({
    color:       s.color,
    fillColor:   s.fillColor,
    fillOpacity: s.fillOpacity,
    weight:      2,
    dashArray:   s.dashArray
  });

  const latlngs = pendingLayer.getLatLngs()[0].map(ll => ({ lat: ll.lat, lng: ll.lng }));

  const zone = {
    id:       Date.now().toString(),
    name,
    type,
    latlngs,
    layerRef: pendingLayer
  };

  // Bind a tooltip to the polygon on the map
  pendingLayer.bindTooltip(
    `<b>${name}</b><br><span style="text-transform:uppercase;font-size:0.72em">${type}</span>`,
    { permanent: false, sticky: true }
  );

  drawnZones.push(zone);
  pendingLayer = null;

  saveZonesToStorage();
  renderZoneList();

  document.getElementById('zoneCreateModal').classList.add('hidden');
  logIncident('safe', `Geofence zone saved: "${name}" [${type.toUpperCase()}]`);
}

/** Delete a saved zone by id */
function deleteZone(id) {
  const idx = drawnZones.findIndex(z => z.id === id);
  if (idx === -1) return;
  const zone = drawnZones[idx];
  if (zone.layerRef && drawnItems) {
    drawnItems.removeLayer(zone.layerRef);
  }
  drawnZones.splice(idx, 1);
  saveZonesToStorage();
  renderZoneList();
  logIncident('info', `Geofence zone deleted: "${zone.name}"`);
}

/** Fly the map to a specific zone */
function zoomToZone(id) {
  const zone = drawnZones.find(z => z.id === id);
  if (!zone || !zone.layerRef || !mapInstance) return;
  mapInstance.fitBounds(zone.layerRef.getBounds(), { padding: [30, 30] });
}

/** Re-render the zone list panel */
function renderZoneList() {
  const list      = document.getElementById('zoneList');
  const countChip = document.getElementById('zoneCountChip');
  if (!list) return;

  list.innerHTML = '';
  if (countChip) countChip.textContent = drawnZones.length + ' Zone' + (drawnZones.length !== 1 ? 's' : '');

  if (drawnZones.length === 0) {
    list.innerHTML = '<div style="font-size:0.75rem;color:var(--text-muted);padding:0.25rem 0.1rem">No custom zones yet. Draw one on the map!</div>';
    return;
  }

  const typeColors = { restricted: 'var(--status-danger)', warning: 'var(--status-warning)', safe: 'var(--status-safe)' };
  const typeLabels = { restricted: 'Restricted', warning: 'Warning',  safe: 'Safe Zone' };

  drawnZones.forEach(zone => {
    const item = document.createElement('div');
    item.className = 'zone-item';
    item.id = 'zone-item-' + zone.id;
    item.innerHTML = `
      <span class="zone-item-dot" style="background:${typeColors[zone.type] || '#8B949E'}"></span>
      <div class="zone-item-info">
        <div class="zone-item-name">${zone.name}</div>
        <div class="zone-item-type">${typeLabels[zone.type] || zone.type}</div>
      </div>
      <div class="zone-item-actions">
        <button class="zone-item-btn btn-zoom" title="Zoom to zone" onclick="zoomToZone('${zone.id}')">
          <i class="fa-solid fa-magnifying-glass-location"></i>
        </button>
        <button class="zone-item-btn" title="Delete zone" onclick="deleteZone('${zone.id}')">
          <i class="fa-solid fa-trash"></i>
        </button>
      </div>
    `;
    list.appendChild(item);
  });

  renderZonesTable();
}

/** Persist zones to localStorage (without layerRef — that's live only) */
function saveZonesToStorage() {
  const serializable = drawnZones.map(z => ({
    id: z.id, name: z.name, type: z.type, latlngs: z.latlngs
  }));
  localStorage.setItem('mineguard_zones', JSON.stringify(serializable));
}

/** Load zones from localStorage and re-draw them on the map */
function loadZonesFromStorage() {
  const raw = localStorage.getItem('mineguard_zones');
  if (!raw) return;
  let saved;
  try { saved = JSON.parse(raw); } catch { return; }

  const styles = {
    restricted: { color: '#EF4444', fillColor: '#EF4444', fillOpacity: 0.18, dashArray: null },
    warning:    { color: '#F59E0B', fillColor: '#F59E0B', fillOpacity: 0.14, dashArray: '6,4' },
    safe:       { color: '#10B981', fillColor: '#10B981', fillOpacity: 0.10, dashArray: '6,4' }
  };

  saved.forEach(z => {
    const latlngs = z.latlngs.map(p => [p.lat, p.lng]);
    const s = styles[z.type] || styles.restricted;
    const layer = L.polygon(latlngs, {
      color: s.color, fillColor: s.fillColor, fillOpacity: s.fillOpacity,
      weight: 2, dashArray: s.dashArray
    }).bindTooltip(
      `<b>${z.name}</b><br><span style="text-transform:uppercase;font-size:0.72em">${z.type}</span>`,
      { permanent: false, sticky: true }
    );
    drawnItems.addLayer(layer);
    drawnZones.push({ id: z.id, name: z.name, type: z.type, latlngs: z.latlngs, layerRef: layer });
  });

  renderZoneList();
  if (drawnZones.length > 0) {
    logIncident('info', `Loaded ${drawnZones.length} saved geofence zone(s) from storage.`);
  }
}

// ============================================================================
// Chart.js Visual Analytics Engine
// ============================================================================
function initCharts() {
  if (typeof Chart === 'undefined') return;

  // 1. Donut Chart — Device Status (Matching Screenshot)
  const ctxDonut = document.getElementById('deviceStatusChart');
  if (ctxDonut) {
    deviceStatusChartInstance = new Chart(ctxDonut, {
      type: 'doughnut',
      data: {
        labels: ['Online', 'Offline'],
        datasets: [{
          data: [1, 0], // Matches 1 Online, 0 Offline (or [1, 2])
          backgroundColor: ['#10B981', '#334155'],
          borderColor: '#0F1B38',
          borderWidth: 5,
          hoverOffset: 4
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        cutout: '76%',
        plugins: {
          legend: { display: false },
          tooltip: {
            backgroundColor: '#14244B',
            borderColor: '#182A56',
            borderWidth: 1,
            titleColor: '#fff',
            bodyColor: '#8E9EB8',
            callbacks: {
              label: (context) => ` ${context.label}: ${context.raw} Device${context.raw !== 1 ? 's' : ''}`
            }
          }
        }
      }
    });
  }

  // 2. Bar Chart — Recent Alert Frequencies (Matching Screenshot)
  const ctxBar = document.getElementById('alertFrequenciesChart');
  if (ctxBar) {
    alertFrequenciesChartInstance = new Chart(ctxBar, {
      type: 'bar',
      data: {
        labels: ['FALL DETECTED', 'GEOFENCE BREACH', 'SUDDEN MOVEMENT'],
        datasets: [{
          label: 'Alert Count',
          data: [alertStats.fall, alertStats.geofence, alertStats.suddenMovement],
          backgroundColor: '#3B82F6',
          borderRadius: 3,
          borderSkipped: false,
          barPercentage: 0.72,
          categoryPercentage: 0.8
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { display: false },
          tooltip: {
            backgroundColor: '#14244B',
            borderColor: '#182A56',
            borderWidth: 1,
            titleColor: '#fff',
            bodyColor: '#8E9EB8'
          }
        },
        scales: {
          x: {
            grid: { display: false },
            ticks: {
              color: '#8E9EB8',
              font: { family: 'Inter', size: 10, weight: '600' }
            }
          },
          y: {
            min: 0,
            max: 4,
            ticks: {
              stepSize: 1,
              color: '#8E9EB8',
              font: { family: 'Inter', size: 11 }
            },
            grid: {
              color: '#182A56',
              drawBorder: false
            }
          }
        }
      }
    });
  }
}

function updateAlertFrequenciesChart() {
  if (!alertFrequenciesChartInstance) return;
  alertFrequenciesChartInstance.data.datasets[0].data = [
    alertStats.fall,
    alertStats.geofence,
    alertStats.suddenMovement
  ];
  // Auto-scale y-axis max if incidents exceed 4
  const maxVal = Math.max(...alertFrequenciesChartInstance.data.datasets[0].data);
  alertFrequenciesChartInstance.options.scales.y.max = Math.max(4, maxVal + 1);
  alertFrequenciesChartInstance.update();
}

// ============================================================================
// Sidebar Tab Switching Navigation
// ============================================================================
function initTabNavigation() {
  const navLinks = document.querySelectorAll('.sidebar-nav .nav-link');
  const tabPanes = document.querySelectorAll('.tab-pane');
  const viewTitle = document.getElementById('viewMainTitle');

  const titles = {
    'tab-dashboard': 'System Overview',
    'tab-live-map':  'Live GPS & Geofencing Map',
    'tab-devices':   'Worker Device Telemetry',
    'tab-zones':     'Safety Geofence Zones',
    'tab-alerts':    'Alerts & Incident Center',
    'tab-history':   'Historical Telemetry Logs'
  };

  navLinks.forEach(link => {
    link.addEventListener('click', (e) => {
      e.preventDefault();
      const targetTabId = link.getAttribute('data-tab');
      if (!targetTabId) return;

      navLinks.forEach(l => l.classList.remove('active'));
      link.classList.add('active');

      tabPanes.forEach(pane => {
        if (pane.id === targetTabId) {
          pane.classList.add('active');
        } else {
          pane.classList.remove('active');
        }
      });

      if (viewTitle && titles[targetTabId]) {
        viewTitle.textContent = titles[targetTabId];
      }

      // Move interactive map section between Live Map and Zones seamlessly
      const mapSection = document.getElementById('interactiveMapSection');
      if (targetTabId === 'tab-live-map') {
        const slotLive = document.getElementById('mapSlotLive');
        if (mapSection && slotLive && mapSection.parentElement !== slotLive) {
          slotLive.appendChild(mapSection);
        }
        ensureMapReady();
      } else if (targetTabId === 'tab-zones') {
        const slotZones = document.getElementById('mapSlotZones');
        if (mapSection && slotZones && mapSection.parentElement !== slotZones) {
          slotZones.appendChild(mapSection);
        }
        renderZonesTable();
        ensureMapReady();
      }

      // Close mobile drawer if opened
      const sidebar = document.getElementById('appSidebar');
      if (sidebar && sidebar.classList.contains('open')) {
        sidebar.classList.remove('open');
      }
    });
  });

  // Mobile sidebar controls
  const toggleBtn = document.getElementById('sidebarToggleBtn');
  const closeBtn  = document.getElementById('sidebarCloseBtn');
  const sidebar   = document.getElementById('appSidebar');

  if (toggleBtn && sidebar) {
    toggleBtn.addEventListener('click', () => sidebar.classList.add('open'));
  }
  if (closeBtn && sidebar) {
    closeBtn.addEventListener('click', () => sidebar.classList.remove('open'));
  }

  // Logout button demo
  const logoutBtn = document.getElementById('logoutBtn');
  if (logoutBtn) {
    logoutBtn.addEventListener('click', () => {
      if (confirm('Are you sure you want to log out of the supervisor dashboard?')) {
        logIncident('info', 'Supervisor session logged out.');
        alert('You have logged out.');
      }
    });
  }
}

/** Ensure Leaflet Map has initialized and properly resized upon tab activation */
function ensureMapReady() {
  if (typeof L === 'undefined') return;
  if (!mapInstance) {
    initLeafletMap();
  }
  setTimeout(() => {
    if (mapInstance) {
      mapInstance.invalidateSize();
      if (workerMarker) {
        mapInstance.setView(workerMarker.getLatLng(), mapInstance.getZoom() || 17);
      }
    }
  }, 100);
  setTimeout(() => {
    if (mapInstance) {
      mapInstance.invalidateSize();
    }
  }, 350);
}

/** Populate Zones Table with default and custom drawn geofences */
function renderZonesTable() {
  const tbody = document.getElementById('zonesTableBody');
  if (!tbody) return;

  const defaultRows = `
    <tr>
      <td><strong>Safe Mine Sector A</strong></td>
      <td><span class="badge badge-safe">Safe Operating Area</span></td>
      <td>Circle (180m radius)</td>
      <td>None (Authorized)</td>
      <td><span class="chip chip-sm" style="background:rgba(255,255,255,0.06);padding:0.15rem 0.45rem;border-radius:4px;font-size:0.7rem">System Default</span></td>
    </tr>
    <tr>
      <td><strong>Deep Mine Toxic Shaft</strong></td>
      <td><span class="badge badge-danger">Restricted Hazard</span></td>
      <td>Circle (70m radius)</td>
      <td>Immediate Siren & Alarm</td>
      <td><span class="chip chip-sm" style="background:rgba(255,255,255,0.06);padding:0.15rem 0.45rem;border-radius:4px;font-size:0.7rem">System Default</span></td>
    </tr>
  `;

  const customRows = drawnZones.map(z => {
    const badgeCls = z.type === 'restricted' ? 'badge-danger' : (z.type === 'warning' ? 'badge-warning' : 'badge-safe');
    const label = z.type === 'restricted' ? 'Restricted Hazard' : (z.type === 'warning' ? 'Caution Warning' : 'Safe Zone');
    const breachAction = z.type === 'restricted' ? 'Immediate Siren & Emergency Banner' : (z.type === 'warning' ? 'Caution Advisory' : 'None (Safe Area)');
    return `
      <tr>
        <td><strong>${z.name}</strong></td>
        <td><span class="badge ${badgeCls}">${label}</span></td>
        <td>Polygon (${z.latlngs.length} vertices)</td>
        <td>${breachAction}</td>
        <td>
          <button class="small-btn" onclick="zoomToZone('${z.id}')" title="Zoom to zone" style="padding:0.2rem 0.5rem;font-size:0.75rem">
            <i class="fa-solid fa-magnifying-glass-location"></i> View
          </button>
          <button class="small-btn" onclick="deleteZone('${z.id}')" title="Delete zone" style="padding:0.2rem 0.5rem;font-size:0.75rem;margin-left:4px;color:var(--status-danger)">
            <i class="fa-solid fa-trash"></i>
          </button>
        </td>
      </tr>
    `;
  }).join('');

  tbody.innerHTML = defaultRows + customRows;
}
