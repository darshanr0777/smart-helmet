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
  
  // Geofencing Center & Mine Boundaries (Latitude, Longitude, Radius in meters)
  mineSiteCenter: { lat: 12.971598, lng: 77.594566 },
  safeZoneRadiusMeters: 180, // Safe operating perimeter
  dangerZoneCenter: { lat: 12.973200, lng: 77.596000 },
  dangerZoneRadiusMeters: 70, // Prohibited Deep Mine Shaft

  // Sensor Telemetry Data
  telemetry: {
    temp: 27.5,
    humidity: 62.0,
    mq3_gas: 0.05,     // mg/L (Flammable / Alcohol / Hydrocarbons)
    mq7_co: 14,        // PPM Carbon Monoxide
    mq135_air: 78,     // PPM Toxic Gas / Air Quality Index
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

  // Threshold Configurations
  thresholds: {
    tempMax: 42.0,     // °C
    tempWarning: 38.0,
    mq7Danger: 50,     // PPM Carbon Monoxide
    mq7Warning: 30,
    mq135Danger: 250,  // PPM Air Quality / Harmful Gases
    mq135Warning: 150,
    mq3Danger: 0.40,   // mg/L Flammable gas
    mq3Warning: 0.20,
    inactivityTimeout: 60, // 60 seconds of zero movement
    fallThresholdG: 2.80   // G-Force impact threshold
  }
};

// Map & Audio handles
let mapInstance = null;
let workerMarker = null;
let safeCircle = null;
let dangerCircle = null;
let audioContext = null;
let sirenOscillator = null;
let sirenGain = null;
let simulatorInterval = null;
let supabaseClient = null;

// ============================================================================
// Initialization
// ============================================================================
document.addEventListener('DOMContentLoaded', () => {
  initLeafletMap();
  initLocalStorageConfig();
  setupEventListeners();
  startSimulator();
  logIncident('info', 'System online. Monitoring sensors for worker ' + state.activeWorker);
});

// ============================================================================
// Leaflet GPS Map & Geofencing Setup
// ============================================================================
function initLeafletMap() {
  const mapElement = document.getElementById('mineMap');
  if (!mapElement) return;

  // Initialize Map with dark tiles
  mapInstance = L.map('mineMap', {
    center: [state.mineSiteCenter.lat, state.mineSiteCenter.lng],
    zoom: 17,
    zoomControl: true
  });

  // OpenStreetMap CartoDB Dark Matter tiles (Perfect for industrial dark dashboards)
  L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
    attribution: '&copy; OpenStreetMap contributors &copy; CARTO',
    subdomains: 'abcd',
    maxZoom: 20
  }).addTo(mapInstance);

  // 1. Authorized Safe Mine Operating Zone (Green Circle)
  safeCircle = L.circle([state.mineSiteCenter.lat, state.mineSiteCenter.lng], {
    color: '#00E676',
    fillColor: '#00E676',
    fillOpacity: 0.12,
    weight: 2,
    dashArray: '6, 6',
    radius: state.safeZoneRadiusMeters
  }).addTo(mapInstance).bindPopup('<b>Safe Mine Sector A</b><br>Approved Operating Perimeter (180m)');

  // 2. Restricted High Hazard Chamber (Red Circle)
  dangerCircle = L.circle([state.dangerZoneCenter.lat, state.dangerZoneCenter.lng], {
    color: '#FF1744',
    fillColor: '#FF1744',
    fillOpacity: 0.25,
    weight: 2,
    radius: state.dangerZoneRadiusMeters
  }).addTo(mapInstance).bindPopup('<b style="color:#FF1744;">RESTRICTED HAZARD SHAFT</b><br>Toxic/Unstable zone. Entry Prohibited!');

  // Custom Helmet Worker Marker Icon
  const helmetIcon = L.divIcon({
    className: 'custom-helmet-marker',
    html: `
      <div style="position:relative; width:34px; height:34px; display:flex; align-items:center; justify-content:center;">
        <div style="position:absolute; width:100%; height:100%; border-radius:50%; background:rgba(255,179,0,0.35); animation:pulse-dot 1.5s infinite;"></div>
        <div style="width:24px; height:24px; border-radius:50%; background:#FFB300; border:2px solid #FFF; display:flex; align-items:center; justify-content:center; box-shadow:0 0 10px #FFB300;">
          <i class="fa-solid fa-hard-hat" style="color:#000; font-size:12px;"></i>
        </div>
      </div>
    `,
    iconSize: [34, 34],
    iconAnchor: [17, 17]
  });

  workerMarker = L.marker([state.telemetry.lat, state.telemetry.lng], { icon: helmetIcon }).addTo(mapInstance);
  workerMarker.bindPopup(`<b>Worker Helmet (W-101)</b><br>Status: Safe<br>Neo-6M GPS Active`);
}

// ============================================================================
// Geofence Calculation (Haversine distance in meters)
// ============================================================================
function calculateDistanceMeters(lat1, lon1, lat2, lon2) {
  const R = 6371e3; // Earth radius in meters
  const φ1 = lat1 * Math.PI / 180;
  const φ2 = lat2 * Math.PI / 180;
  const Δφ = (lat2 - lat1) * Math.PI / 180;
  const Δλ = (lon2 - lon1) * Math.PI / 180;

  const a = Math.sin(Δφ/2) * Math.sin(Δφ/2) +
            Math.cos(φ1) * Math.cos(φ2) *
            Math.sin(Δλ/2) * Math.sin(Δλ/2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
  return R * c;
}

function evaluateGeofence(lat, lng) {
  const distFromCenter = calculateDistanceMeters(lat, lng, state.mineSiteCenter.lat, state.mineSiteCenter.lng);
  const distToRestricted = calculateDistanceMeters(lat, lng, state.dangerZoneCenter.lat, state.dangerZoneCenter.lng);

  const kpiGeofenceText = document.getElementById('kpiGeofenceText');
  const kpiDistanceText = document.getElementById('kpiDistanceText');

  // Case 1: Inside Dangerous Chamber
  if (distToRestricted <= state.dangerZoneRadiusMeters) {
    kpiGeofenceText.textContent = 'RESTRICTED AREA BREACH!';
    kpiGeofenceText.className = 'kpi-value text-danger';
    kpiDistanceText.textContent = `Inside Toxic Shaft (${Math.round(distToRestricted)}m from epicenter)`;
    return { status: 'BREACH_RESTRICTED', message: 'CRITICAL: Worker entered restricted toxic hazard shaft!' };
  }

  // Case 2: Out of Safe Mine Perimeter
  if (distFromCenter > state.safeZoneRadiusMeters) {
    kpiGeofenceText.textContent = 'OUT OF BOUNDS';
    kpiGeofenceText.className = 'kpi-value text-danger';
    kpiDistanceText.textContent = `${Math.round(distFromCenter - state.safeZoneRadiusMeters)}m outside perimeter`;
    return { status: 'OUT_OF_BOUNDS', message: 'WARNING: Worker strayed outside authorized mine perimeter!' };
  }

  // Case 3: Inside Safe Zone
  const distToBoundary = Math.max(0, Math.round(state.safeZoneRadiusMeters - distFromCenter));
  kpiGeofenceText.textContent = 'INSIDE SAFE ZONE';
  kpiGeofenceText.className = 'kpi-value text-safe';
  kpiDistanceText.textContent = `Dist to Perimeter: ~${distToBoundary}m`;
  return { status: 'SAFE', message: '' };
}

// ============================================================================
// UI Updates & Telemetry Evaluation
// ============================================================================
function updateDashboardUI() {
  const d = state.telemetry;

  // 1. Environmental: DHT11
  document.getElementById('tempVal').textContent = d.temp.toFixed(1);
  document.getElementById('humidVal').textContent = Math.round(d.humidity);
  
  const tempGauge = document.getElementById('tempGauge');
  const tempBadge = document.getElementById('tempBadge');
  if (d.temp >= state.thresholds.tempMax) {
    tempBadge.textContent = 'Extreme Heat';
    tempBadge.className = 'badge badge-danger';
    tempGauge.style.borderColor = 'var(--danger-crimson)';
    tempGauge.style.boxShadow = '0 0 15px var(--danger-glow)';
  } else if (d.temp >= state.thresholds.tempWarning) {
    tempBadge.textContent = 'Warning';
    tempBadge.className = 'badge badge-warning';
    tempGauge.style.borderColor = 'var(--safety-amber)';
    tempGauge.style.boxShadow = '0 0 15px var(--safety-amber-glow)';
  } else {
    tempBadge.textContent = 'Optimal';
    tempBadge.className = 'badge badge-safe';
    tempGauge.style.borderColor = 'var(--cyber-cyan)';
    tempGauge.style.boxShadow = '0 0 15px var(--cyber-cyan-glow)';
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

  // 4. Gas Array: MQ-3 (Alcohol / Flammable vapors)
  document.getElementById('mq3Val').textContent = d.mq3_gas.toFixed(2);
  const mq3Progress = document.getElementById('mq3Progress');
  const mq3Badge = document.getElementById('mq3Badge');
  const mq3Percent = Math.min(100, (d.mq3_gas / 0.8) * 100);
  mq3Progress.style.width = mq3Percent + '%';

  if (d.mq3_gas >= state.thresholds.mq3Danger) {
    mq3Badge.textContent = 'FLAMMABLE RISK';
    mq3Badge.className = 'badge badge-danger';
    mq3Progress.className = 'progress-fill fill-danger';
  } else if (d.mq3_gas >= state.thresholds.mq3Warning) {
    mq3Badge.textContent = 'Traces Detected';
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
  if (d.mq3_gas >= state.thresholds.mq3Danger) {
    hazards.push(`Flammable Gas Hazard: ${d.mq3_gas.toFixed(2)} mg/L`);
  }
  if (d.temp >= state.thresholds.tempMax) {
    hazards.push(`Critical High Heat: ${d.temp.toFixed(1)}°C`);
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
    kpiGasSummary.textContent = hazards[0];

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
    kpiGasSummary.textContent = 'MQ-3, MQ-7, MQ-135 Nominal';

    emergencyBanner.classList.add('hidden');
    triggerAudioAlarm(false);
  }

  const now = new Date();
  document.getElementById('lastUpdatedTime').textContent = `Last sync: ${now.toLocaleTimeString()}`;
}

// ============================================================================
// Web Audio API Siren / Alarm
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
    state.telemetry.mq7_co += (Math.random() - 0.5) * 0.8;
    state.telemetry.mq135_air += (Math.random() - 0.5) * 1.5;
    state.telemetry.mq3_gas += (Math.random() - 0.5) * 0.005;

    // Bounds safety clamp
    state.telemetry.temp = Math.max(20, Math.min(50, state.telemetry.temp));
    state.telemetry.mq7_co = Math.max(5, state.telemetry.mq7_co);
    state.telemetry.mq135_air = Math.max(40, state.telemetry.mq135_air);
    state.telemetry.mq3_gas = Math.max(0.01, state.telemetry.mq3_gas);

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
    updateDashboardUI();
  });

  document.getElementById('simHighTempBtn').addEventListener('click', () => {
    state.telemetry.temp = 44.5; // Above 42°C danger
    state.telemetry.humidity = 88;
    logIncident('danger', 'SIMULATION TRIGGER: Mine shaft extreme heat wave (44.5°C)!');
    updateDashboardUI();
  });

  document.getElementById('simFallBtn').addEventListener('click', () => {
    state.isFallDetected = true;
    state.telemetry.accelTotal = 3.65; // High impact shock (> 2.80G)
    logIncident('danger', 'SIMULATION TRIGGER: 3.65G impact registered. Worker fall detected!');
    updateDashboardUI();
  });

  document.getElementById('simInactivityBtn').addEventListener('click', () => {
    state.inactivitySeconds = 65; // Above 60s timeout
    logIncident('danger', 'SIMULATION TRIGGER: Worker inactive and unresponsive for 65 seconds!');
    updateDashboardUI();
  });

  document.getElementById('simResetNormalBtn').addEventListener('click', () => {
    state.telemetry.temp = 27.5;
    state.telemetry.humidity = 62;
    state.telemetry.mq7_co = 14;
    state.telemetry.mq135_air = 78;
    state.telemetry.mq3_gas = 0.05;
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
}

// ============================================================================
// Supabase Integration Handlers
// ============================================================================
function initLocalStorageConfig() {
  const savedUrl = localStorage.getItem('mineguard_supabase_url');
  const savedKey = localStorage.getItem('mineguard_supabase_key');
  const savedSource = localStorage.getItem('mineguard_data_source') || 'sim';

  if (savedUrl) document.getElementById('supabaseUrlInput').value = savedUrl;
  if (savedKey) document.getElementById('supabaseAnonKeyInput').value = savedKey;
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

function initSupabaseConnection(url, key) {
  if (!window.supabase) {
    console.error('Supabase client library not loaded');
    return;
  }

  try {
    supabaseClient = window.supabase.createClient(url, key);
    document.getElementById('connectionStatusText').textContent = 'CONNECTING SUPABASE...';

    // Subscribe to Realtime inserts on 'helmet_telemetry' table
    supabaseClient
      .channel('public:helmet_telemetry')
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'helmet_telemetry' }, (payload) => {
        const row = payload.new;
        if (row.worker_id === state.activeWorker || !row.worker_id) {
          receiveHardwareTelemetry(row);
        }
      })
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') {
          document.getElementById('connectionStatusText').textContent = 'SUPABASE CLOUD LIVE';
          logIncident('safe', 'Connected to Supabase Realtime channel.');
        } else if (status === 'CLOSED' || status === 'CHANNEL_ERROR') {
          document.getElementById('connectionStatusText').textContent = 'SUPABASE OFFLINE';
        }
      });
  } catch (err) {
    console.error('Failed to init Supabase:', err);
    logIncident('warning', 'Failed to connect to Supabase: ' + err.message);
  }
}

function receiveHardwareTelemetry(row) {
  if (row.temperature !== undefined) state.telemetry.temp = parseFloat(row.temperature);
  if (row.humidity !== undefined) state.telemetry.humidity = parseFloat(row.humidity);
  if (row.mq7_co !== undefined) state.telemetry.mq7_co = parseFloat(row.mq7_co);
  if (row.mq135_air !== undefined) state.telemetry.mq135_air = parseFloat(row.mq135_air);
  if (row.mq3_gas !== undefined) state.telemetry.mq3_gas = parseFloat(row.mq3_gas);
  if (row.latitude !== undefined) state.telemetry.lat = parseFloat(row.latitude);
  if (row.longitude !== undefined) state.telemetry.lng = parseFloat(row.longitude);
  if (row.accel_total !== undefined) state.telemetry.accelTotal = parseFloat(row.accel_total);
  if (row.is_fall !== undefined) state.isFallDetected = Boolean(row.is_fall);
  if (row.inactivity_secs !== undefined) state.inactivitySeconds = parseInt(row.inactivity_secs);

  logIncident('info', `Hardware payload received from ESP32 (${state.activeWorker}).`);
  updateDashboardUI();
}
