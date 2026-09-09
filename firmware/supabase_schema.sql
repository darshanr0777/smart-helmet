-- ==============================================================================
-- Project: IOT BASED SMART SAFETY HELMET FOR MINE WORKERS
-- Supabase Database Schema Migration
-- ==============================================================================

-- 1. Create table for live worker telemetry
CREATE TABLE IF NOT EXISTS public.helmet_telemetry (
    id BIGSERIAL PRIMARY KEY,
    worker_id VARCHAR(50) NOT NULL DEFAULT 'W-101',
    temperature NUMERIC(5, 2),
    humidity NUMERIC(5, 2),
    mq7_co NUMERIC(6, 2),         -- Carbon monoxide (PPM)
    mq135_air NUMERIC(6, 2),      -- Toxic air quality (PPM)
    mq3_gas NUMERIC(6, 3),        -- Flammable gas (mg/L)
    latitude DOUBLE PRECISION,    -- GPS Lat
    longitude DOUBLE PRECISION,   -- GPS Lng
    accel_total NUMERIC(5, 2),    -- Total G-force (MPU6050)
    is_fall BOOLEAN DEFAULT FALSE,
    inactivity_secs INTEGER DEFAULT 0,
    gps_fix BOOLEAN DEFAULT FALSE,        -- GPS has valid satellite fix
    gps_satellites INTEGER DEFAULT 0,     -- Number of satellites in view
    gps_hdop NUMERIC(4, 1) DEFAULT 99.9, -- Horizontal Dilution of Precision (lower = better)
    alert_level VARCHAR(20) DEFAULT 'SAFE', -- 'SAFE', 'WARNING', 'DANGER'
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- 2. Create table for historical hazard events & incident alerts
CREATE TABLE IF NOT EXISTS public.mine_alerts (
    id BIGSERIAL PRIMARY KEY,
    worker_id VARCHAR(50) NOT NULL,
    alert_type VARCHAR(50) NOT NULL, -- 'GAS_LEAK', 'HEAT_STRESS', 'FALL', 'INACTIVITY', 'GEOFENCE_BREACH'
    severity VARCHAR(20) NOT NULL,   -- 'WARNING', 'CRITICAL'
    details TEXT,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    is_acknowledged BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- 3. Enable Row Level Security (RLS) but allow anonymous inserts/selects for demo & ESP32
ALTER TABLE public.helmet_telemetry ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.mine_alerts ENABLE ROW LEVEL SECURITY;

-- Allow anon key to insert telemetry from ESP32
DROP POLICY IF EXISTS "Allow anon insert telemetry" ON public.helmet_telemetry;
CREATE POLICY "Allow anon insert telemetry" 
ON public.helmet_telemetry FOR INSERT 
TO anon 
WITH CHECK (true);

-- Allow dashboard to read telemetry
DROP POLICY IF EXISTS "Allow anon read telemetry" ON public.helmet_telemetry;
CREATE POLICY "Allow anon read telemetry" 
ON public.helmet_telemetry FOR SELECT 
TO anon 
USING (true);

-- Allow alerts access
DROP POLICY IF EXISTS "Allow anon insert alerts" ON public.mine_alerts;
CREATE POLICY "Allow anon insert alerts" 
ON public.mine_alerts FOR ALL 
TO anon 
USING (true)
WITH CHECK (true);

-- 4. Enable Realtime Publications for the tables so the Web Dashboard updates live!
BEGIN;
  DROP PUBLICATION IF EXISTS supabase_realtime;
  CREATE PUBLICATION supabase_realtime;
COMMIT;
ALTER PUBLICATION supabase_realtime ADD TABLE public.helmet_telemetry;
ALTER PUBLICATION supabase_realtime ADD TABLE public.mine_alerts;

-- 5. Migration: Add GPS quality columns if table already exists
-- Run these if your table was created before the GPS update:
ALTER TABLE public.helmet_telemetry
  ADD COLUMN IF NOT EXISTS gps_fix BOOLEAN DEFAULT FALSE,
  ADD COLUMN IF NOT EXISTS gps_satellites INTEGER DEFAULT 0,
  ADD COLUMN IF NOT EXISTS gps_hdop NUMERIC(4, 1) DEFAULT 99.9;

