local script = {}

local scene = oxygen.scene
local omath = oxygen.math
local log = oxygen.log

local initialized = false
local elapsed_seconds = 0.0
local cached_sun_node = nil

local function clamp(x, lo, hi)
  if x < lo then
    return lo
  end
  if x > hi then
    return hi
  end
  return x
end

local function lerp(a, b, t)
  return a + (b - a) * t
end

local function valid(node)
  return node ~= nil and node:is_alive()
end

local function find_sun_node()
  if valid(cached_sun_node) then
    return cached_sun_node
  end
  cached_sun_node = scene.find_one("Sun")
  return cached_sun_node
end

local function apply_sun_node_settings(sun_node, azimuth_deg, elevation_deg, color_rgb, lux)
  if not valid(sun_node) then
    return
  end

  if not sun_node:has_light() then
    sun_node:attach_directional_light({
      affects_world = true,
      casts_shadows = true,
      environment_contribution = true,
      is_sun_light = true,
      intensity_lux = lux,
      color_rgb = color_rgb,
      angular_size_radians = 0.00935,
    })
  end

  sun_node:light_set_is_sun_light(true)
  sun_node:light_set_environment_contribution(true)
  sun_node:light_set_casts_shadows(true)
  sun_node:light_set_intensity_lux(lux)
  sun_node:light_set_color_rgb(color_rgb)
  sun_node:light_set_angular_size_radians(0.00935)

  local pitch = math.rad(-elevation_deg)
  local yaw = math.rad(azimuth_deg)
  sun_node:set_local_rotation(omath.quat_from_euler_xyz(pitch, 0.0, yaw))
end

local function apply_environment_settings(azimuth_deg, elevation_deg, color_rgb, lux, day01, sun_disk_enabled)
  local env = scene.ensure_environment()
  if env == nil then
    return
  end

  local sky_atmo = env:ensure_sky_atmosphere()
  if sky_atmo ~= nil then
    sky_atmo:set({ sun_disk_enabled = sun_disk_enabled })
  end

  local clouds = env:ensure_clouds()
  if clouds ~= nil then
    local coverage = lerp(0.22, 0.48, 0.5 + 0.5 * math.sin((elapsed_seconds * 0.17) + 0.35))
    local wind_dir = omath.vec3(math.cos(elapsed_seconds * 0.07), math.sin(elapsed_seconds * 0.07), 0.0)
    clouds:set({
      coverage = coverage,
      wind_direction_ws = wind_dir,
      wind_speed_mps = 8.0,
      shadow_strength = lerp(0.25, 0.65, day01),
    })
  end
end

function script.on_gameplay(ctx, dt_seconds)
  local _ = ctx
  local cycle_seconds = scene.param(ctx, "day_duration_seconds") or 60.0
  if cycle_seconds <= 1.0 then
    cycle_seconds = 60.0
  end

  elapsed_seconds = elapsed_seconds + dt_seconds
  local t = (elapsed_seconds % cycle_seconds) / cycle_seconds
  local theta = (t * (2.0 * math.pi)) - (0.5 * math.pi)

  local day01 = clamp(0.5 + 0.5 * math.sin(theta), 0.0, 1.0)
  local elevation_deg = lerp(-12.0, 75.0, day01)
  local azimuth_deg = (t * 360.0) % 360.0
  local sun_above_horizon = clamp((elevation_deg + 1.0) / 11.0, 0.0, 1.0)
  local sun_disk_enabled = elevation_deg > -0.5

  local warm_r = lerp(1.0, 1.0, day01)
  local warm_g = lerp(0.52, 0.98, day01)
  local warm_b = lerp(0.30, 0.93, day01)
  local color_r = lerp(0.24, warm_r, day01)
  local color_g = lerp(0.32, warm_g, day01)
  local color_b = lerp(0.55, warm_b, day01)
  local color_rgb = omath.vec3(color_r, color_g, color_b)

  local lux = lerp(0.001, 120000.0, sun_above_horizon * sun_above_horizon)

  local sun_node = find_sun_node()
  if valid(sun_node) then
    sun_node:light_set_affects_world(sun_above_horizon > 0.01)
  end
  apply_sun_node_settings(sun_node, azimuth_deg, elevation_deg, color_rgb, lux)
  apply_environment_settings(
    azimuth_deg, elevation_deg, color_rgb, lux, day01, sun_disk_enabled)

  if not initialized then
    initialized = true
    log.info("time_of_day: enabled (60s day cycle)")
  end
end

return script
