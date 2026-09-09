-- ==============================================================================
-- Project : IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
-- File    : supabase_schema.sql
-- Purpose : Full database schema + migration scripts
--
-- HOW TO RUN THIS IN SUPABASE:
--   1. Open Supabase Dashboard → your project
--   2. Click "SQL Editor" in the left sidebar
--   3. Paste this entire file and click "Run"
--   4. You should see "Success. No rows returned" for each statement.
--
-- TABLES CREATED:
--   • helmet_telemetry  — live sensor readings from ESP32
--   • mine_alerts       — historical hazard events & incidents
-- ==============================================================================


-- ============================================================================
-- STEP 1: Create the live telemetry table
-- ============================================================================
-- Stores every sensor reading pushed by the ESP32 every 3 seconds.
-- Columns cover: DHT11, MQ-2, MQ-7, MQ-135, GPS, MPU-6050.
-- "IF NOT EXISTS" makes this safe to re-run on an existing database.
-- ============================================================================
CREATE TABLE IF NOT EXISTS public.helmet_telemetry (
    id              BIGSERIAL PRIMARY KEY,

    -- Worker identification
    worker_id       VARCHAR(50)  NOT NULL DEFAULT 'W-101',

    -- DHT11 — Temperature & Humidity
    temperature     NUMERIC(5, 2),                       -- °C
    humidity        NUMERIC(5, 2),                       -- %RH

    -- MQ-2  — Smoke / LPG / Flammable Gas (PPM)
    -- Mapped from ESP32 sketch PIN_MQ2 (GPIO 34)
    mq2_smoke       NUMERIC(7, 2),                       -- PPM  ← NEW column

    -- MQ-7  — Carbon Monoxide (PPM)
    -- Mapped from ESP32 sketch PIN_MQ7 (GPIO 35)
    mq7_co          NUMERIC(6, 2),                       -- PPM

    -- MQ-135 — Air Quality / NH3 / NOx / Benzene (PPM)
    -- Mapped from ESP32 sketch PIN_MQ135 (GPIO 32)
    mq135_air       NUMERIC(6, 2),                       -- PPM

    -- MQ-3 / Legacy gas column (kept for backward compatibility with
    -- the main smart_helmet_esp32.ino which uses MQ-3 on GPIO 34)
    mq3_gas         NUMERIC(6, 3),                       -- mg/L

    -- GPS Neo-6M — Location
    latitude        DOUBLE PRECISION,
    longitude       DOUBLE PRECISION,
    gps_fix         BOOLEAN      DEFAULT FALSE,           -- Valid satellite lock
    gps_satellites  INTEGER      DEFAULT 0,              -- Satellites in view
    gps_hdop        NUMERIC(4, 1) DEFAULT 99.9,          -- Horiz. Dilution (lower = better)

    -- MPU-6050 — Motion / Fall Detection
    accel_total     NUMERIC(5, 2),                       -- Total G-force
    is_fall         BOOLEAN      DEFAULT FALSE,           -- Fall impact flag
    inactivity_secs INTEGER      DEFAULT 0,              -- Seconds of no movement

    -- Alert classification (set by ESP32 firmware)
    alert_level     VARCHAR(20)  DEFAULT 'SAFE',         -- 'SAFE' | 'WARNING' | 'DANGER'

    -- Auto timestamp (UTC)
    created_at      TIMESTAMPTZ  DEFAULT NOW()
);

-- Add a comment on the table for documentation
COMMENT ON TABLE public.helmet_telemetry IS
    'Real-time sensor telemetry from ESP32 smart safety helmets.';


-- ============================================================================
-- STEP 2: Create the historical alerts / incidents table
-- ============================================================================
CREATE TABLE IF NOT EXISTS public.mine_alerts (
    id              BIGSERIAL PRIMARY KEY,
    worker_id       VARCHAR(50)  NOT NULL,
    alert_type      VARCHAR(50)  NOT NULL,  -- 'GAS_LEAK' | 'HEAT_STRESS' | 'FALL' |
                                            -- 'INACTIVITY' | 'GEOFENCE_BREACH' |
                                            -- 'SMOKE' | 'CO_HIGH'
    severity        VARCHAR(20)  NOT NULL,  -- 'WARNING' | 'CRITICAL'
    details         TEXT,
    latitude        DOUBLE PRECISION,
    longitude       DOUBLE PRECISION,
    is_acknowledged BOOLEAN      DEFAULT FALSE,
    created_at      TIMESTAMPTZ  DEFAULT NOW()
);

COMMENT ON TABLE public.mine_alerts IS
    'Historical hazard events and alert incidents for audit trail.';


-- ============================================================================
-- STEP 3: Row Level Security (RLS)
-- Enables RLS but creates open policies so the ESP32 anon key
-- can INSERT telemetry and the dashboard can SELECT it.
-- For production, restrict these policies per worker_id or JWT role.
-- ============================================================================
ALTER TABLE public.helmet_telemetry ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mine_alerts      ENABLE ROW LEVEL SECURITY;

-- ESP32 → INSERT telemetry
DROP POLICY IF EXISTS "Allow anon insert telemetry" ON public.helmet_telemetry;
CREATE POLICY "Allow anon insert telemetry"
    ON public.helmet_telemetry FOR INSERT
    TO anon
    WITH CHECK (true);

-- Dashboard → SELECT telemetry
DROP POLICY IF EXISTS "Allow anon read telemetry" ON public.helmet_telemetry;
CREATE POLICY "Allow anon read telemetry"
    ON public.helmet_telemetry FOR SELECT
    TO anon
    USING (true);

-- Dashboard / ESP32 → Full access to mine_alerts
DROP POLICY IF EXISTS "Allow anon insert alerts" ON public.mine_alerts;
CREATE POLICY "Allow anon insert alerts"
    ON public.mine_alerts FOR ALL
    TO anon
    USING (true)
    WITH CHECK (true);


-- ============================================================================
-- STEP 4: Enable Realtime so the Web Dashboard updates live via WebSocket
-- ============================================================================
BEGIN;
  DROP PUBLICATION IF EXISTS supabase_realtime;
  CREATE PUBLICATION supabase_realtime;
COMMIT;

ALTER PUBLICATION supabase_realtime ADD TABLE public.helmet_telemetry;
ALTER PUBLICATION supabase_realtime ADD TABLE public.mine_alerts;


-- ============================================================================
-- STEP 5: MIGRATION — Run these if your table ALREADY EXISTS
-- (Safe to run even on a fresh table — ADD COLUMN IF NOT EXISTS is idempotent)
-- ============================================================================

-- Migration 5a: Add GPS quality columns (added in v1.1)
ALTER TABLE public.helmet_telemetry
    ADD COLUMN IF NOT EXISTS gps_fix        BOOLEAN      DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS gps_satellites INTEGER      DEFAULT 0,
    ADD COLUMN IF NOT EXISTS gps_hdop       NUMERIC(4,1) DEFAULT 99.9;

-- Migration 5b: Add MQ-2 smoke column (added in v3.0 — multi-sensor update)
-- This separates MQ-2 (smoke/LPG) from the legacy MQ-3 column.
ALTER TABLE public.helmet_telemetry
    ADD COLUMN IF NOT EXISTS mq2_smoke NUMERIC(7, 2);

COMMENT ON COLUMN public.helmet_telemetry.mq2_smoke IS
    'MQ-2 sensor reading — Smoke / LPG / Flammable gas in PPM. GPIO 34 on ESP32.';

COMMENT ON COLUMN public.helmet_telemetry.mq7_co IS
    'MQ-7 sensor reading — Carbon Monoxide (CO) in PPM. GPIO 35 on ESP32.';

COMMENT ON COLUMN public.helmet_telemetry.mq135_air IS
    'MQ-135 sensor reading — Air quality / NH3 / NOx in PPM. GPIO 32 on ESP32.';

COMMENT ON COLUMN public.helmet_telemetry.mq3_gas IS
    'MQ-3 sensor reading — Flammable / Alcohol gas in mg/L. Legacy column (GPIO 34).';

COMMENT ON COLUMN public.helmet_telemetry.alert_level IS
    'Overall alert status computed by ESP32: SAFE | WARNING | DANGER';


-- ============================================================================
-- STEP 6: Useful helper VIEWS for the dashboard
-- ============================================================================

-- View: Latest reading per worker
CREATE OR REPLACE VIEW public.latest_telemetry AS
SELECT DISTINCT ON (worker_id)
    worker_id,
    temperature,
    humidity,
    mq2_smoke,
    mq7_co,
    mq135_air,
    alert_level,
    latitude,
    longitude,
    created_at
FROM public.helmet_telemetry
ORDER BY worker_id, created_at DESC;

COMMENT ON VIEW public.latest_telemetry IS
    'Most recent sensor row for each worker — useful for dashboard summary cards.';

-- View: All DANGER events in last 24 hours
CREATE OR REPLACE VIEW public.recent_danger_events AS
SELECT
    id,
    worker_id,
    temperature,
    humidity,
    mq2_smoke,
    mq7_co,
    mq135_air,
    alert_level,
    created_at
FROM public.helmet_telemetry
WHERE alert_level = 'DANGER'
  AND created_at  >= NOW() - INTERVAL '24 hours'
ORDER BY created_at DESC;

COMMENT ON VIEW public.recent_danger_events IS
    'All DANGER-level telemetry rows from the last 24 hours.';


-- ============================================================================
-- DONE!
-- After running this script:
--   1. Your helmet_telemetry table is ready to receive ESP32 data.
--   2. Realtime is enabled — the dashboard will update instantly.
--   3. The mq2_smoke column is available for MQ-2 sensor readings.
--   4. Run your ESP32 sketch — open Serial Monitor to confirm uploads.
-- ============================================================================
