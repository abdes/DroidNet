local script = {}

local scene = oxygen.scene
local assets = oxygen.assets
local input = oxygen.input
local otime = oxygen.time
local log = oxygen.log
local omath = oxygen.math

local node_ref = nil
local host_runtime_id = nil
local orb_ref = nil
local speed = 1.6
local direction = 1.0
local bounce_phase = 0.0
local orb_phase = 0.0
local pulse_timer = 0.0
local shape_index = 1
local shapes = { "cube", "sphere", "cone", "torus", "cylinder", "quad" }
local input_wired = false
local boot_logged = false
local speed_initialized = false
local movement_enabled = true
local last_tick_node_tag = ""
local orb_toggle_requested = false
local pending_material_loads = {}

local function valid(node)
  return node ~= nil and node:is_alive()
end

local function node_tag(node)
  if not valid(node) then
    return "dead"
  end
  local id = node:runtime_id()
  return string.format(
    "%s[sid=%d idx=%d]",
    node:get_name() or "?",
    id.scene_id or -1,
    id.node_index or -1)
end

local function current_shape()
  return shapes[shape_index]
end

local function request_material_once(guid, on_ready)
  if type(guid) ~= "string" or #guid == 0 then
    return
  end
  if pending_material_loads[guid] == true then
    return
  end
  pending_material_loads[guid] = true
  assets.load_material_async(guid, function(mat)
    pending_material_loads[guid] = nil
    if mat == nil then
      log.debug("showcase: material load miss/fail " .. guid)
      return
    end
    on_ready(mat)
  end)
end

local function is_orb_node(node)
  return valid(node) and node:get_name() == "CompanionOrb"
end

local function same_runtime_id(a, b)
  if a == nil or b == nil then
    return false
  end
  return a.scene_id == b.scene_id and a.node_index == b.node_index
end

local function resolve_host_node(ctx, allow_lock)
  local current = scene.current_node(ctx)
  if not valid(current) or is_orb_node(current) then
    return nil
  end

  if host_runtime_id == nil then
    if not allow_lock then
      return nil
    end
    host_runtime_id = current:runtime_id()
    node_ref = current
    log.info("showcase: host node locked -> " .. node_tag(current))
    return current
  end

  local current_id = current:runtime_id()
  if same_runtime_id(current_id, host_runtime_id) then
    node_ref = current
    return current
  end

  return nil
end

local function apply_shape(node)
  local geometry = assets.create_procedural_geometry(current_shape())
  if geometry == nil then
    log.error("showcase: failed to create geometry for " .. current_shape())
    return false
  end

  if not node:renderable_set_geometry(geometry) then
    log.error("showcase: renderable_set_geometry failed for " .. current_shape())
    return false
  end

  return true
end

local function ensure_renderable(node)
  if not valid(node) then
    return false
  end

  if node:has_renderable() then
    return true
  end

  return apply_shape(node)
end

local function toggle_orb(parent_node)
  if valid(orb_ref) then
    log.info("showcase: destroy orb requested orb=" .. node_tag(orb_ref)
      .. " main=" .. node_tag(parent_node))
    scene.destroy_node(orb_ref)
    orb_ref = nil
    log.info("showcase: companion orb destroyed")
    return
  end

  if not valid(parent_node) then
    return
  end

  orb_ref = scene.create_node("CompanionOrb", parent_node)
  if not valid(orb_ref) then
    log.error("showcase: failed to create companion orb")
    return
  end

  orb_ref:set_local_position(omath.vec3(2.0, 0.5, 0.0))
  local orb_geo = assets.create_procedural_geometry("sphere", {
    latitude_segments = 12,
    longitude_segments = 16,
  })
  local orb_mat = assets.create_debug_material()
  if orb_geo ~= nil then
    orb_ref:renderable_set_geometry(orb_geo)
  end
  if orb_mat ~= nil then
    orb_ref:renderable_set_material_override(1, 1, orb_mat)
  end
  log.info("showcase: companion orb spawned orb=" .. node_tag(orb_ref)
    .. " main=" .. node_tag(parent_node))
end

local function wire_input_once()
  if input_wired then
    return
  end

  input.on_action("RotateSpeedUpAction", input.edges.triggered, function()
    movement_enabled = true
    speed = speed + 0.25
    log.debug(string.format("showcase speed=%.2f", speed))
  end)

  input.on_action("RotateSpeedDownAction", input.edges.triggered, function()
    movement_enabled = true
    speed = speed - 0.25
    if speed < 0.0 then
      speed = 0.0
    end
    log.debug(string.format("showcase speed=%.2f", speed))
  end)

  input.on_action("RotateStopAction", input.edges.triggered, function()
    movement_enabled = false
    log.debug("showcase movement stopped")
  end)

  input.on_action("RotateInvertAction", input.edges.triggered, function()
    direction = -direction
    log.info(direction > 0.0 and "showcase direction: clockwise"
      or "showcase direction: counter-clockwise")
  end)

  input.on_action("CycleShapeAction", input.edges.triggered, function()
    shape_index = (shape_index % #shapes) + 1
    if valid(node_ref) then
      apply_shape(node_ref)
      log.info("showcase shape: " .. current_shape())
    end
  end)

  input.on_action("SpawnOrbAction", input.edges.triggered, function()
    orb_toggle_requested = true
  end)

  input.on_action("PulseScaleAction", input.edges.triggered, function()
    pulse_timer = 0.22
    log.debug("showcase pulse")
  end)

  input_wired = true
end

function script.on_gameplay(ctx, dt_seconds)
  if not valid(node_ref) then
    host_runtime_id = nil
  end
  local host_node = resolve_host_node(ctx, true)
  if not valid(host_node) then
    return
  end
  local tick_tag = node_tag(host_node)
  if tick_tag ~= last_tick_node_tag then
    log.info("showcase: tick node switched -> " .. tick_tag)
    last_tick_node_tag = tick_tag
  end

  if not speed_initialized then
    speed = scene.param(ctx, "speed") or speed
    speed_initialized = true
  end
  local bounce_amp = scene.param(ctx, "bounce_amplitude") or 0.45

  local host_mat_guid = scene.param(ctx, "host_material_guid")
  local orb_mat_guid = scene.param(ctx, "orb_material_guid")

  if not boot_logged then
    log.info("showcase boot: assets.available="
      .. tostring(assets.available())
      .. " assets.enabled="
      .. tostring(assets.enabled()))
    boot_logged = true
  end

  if not ensure_renderable(host_node) then
    return
  end

  if type(host_mat_guid) == "string" and #host_mat_guid > 0 then
    local host_mat = assets.get_material(host_mat_guid)
    if host_mat ~= nil then
      host_node:renderable_set_material_override(1, 1, host_mat)
    elseif not assets.has_material(host_mat_guid) then
      request_material_once(host_mat_guid, function(mat)
        if valid(node_ref) then
          node_ref:renderable_set_material_override(1, 1, mat)
        end
      end)
    end
  end

  wire_input_once()

  local dt = otime.delta_seconds and otime.delta_seconds() or dt_seconds

  if not movement_enabled then
    if pulse_timer > 0.0 then
      pulse_timer = pulse_timer - dt
      local t = pulse_timer / 0.22
      local s = 1.0 + math.sin((1.0 - t) * 12.0) * 0.12
      host_node:set_local_scale(omath.vec3(s, s, s))
    else
      host_node:set_local_scale(omath.vec3(1.0, 1.0, 1.0))
    end
    return
  end

  local angle = dt * speed * direction
  if angle ~= 0.0 then
    host_node:rotate(omath.quat_from_euler_xyz(0.0, angle, angle * 0.2), true)
  end

  bounce_phase = bounce_phase + dt * (1.25 + speed * 0.2)
  local y = math.sin(bounce_phase) * bounce_amp
  host_node:set_local_position(omath.vec3(0.0, y, 0.0))

  if pulse_timer > 0.0 then
    pulse_timer = pulse_timer - dt
    local t = pulse_timer / 0.22
    local s = 1.0 + math.sin((1.0 - t) * 12.0) * 0.12
    host_node:set_local_scale(omath.vec3(s, s, s))
  else
    host_node:set_local_scale(omath.vec3(1.0, 1.0, 1.0))
  end

  if valid(orb_ref) then
    if type(orb_mat_guid) == "string" and #orb_mat_guid > 0 then
      local orb_mat = assets.get_material(orb_mat_guid)
      if orb_mat ~= nil then
        orb_ref:renderable_set_material_override(1, 1, orb_mat)
      elseif not assets.has_material(orb_mat_guid) then
        request_material_once(orb_mat_guid, function(mat)
          if valid(orb_ref) then
            orb_ref:renderable_set_material_override(1, 1, mat)
          end
        end)
      end
    end

    orb_phase = orb_phase + dt * 1.8
    local radius = 1.8
    local ox = math.cos(orb_phase) * radius
    local oz = math.sin(orb_phase) * radius
    orb_ref:set_local_position(omath.vec3(ox, 0.6, oz))
    orb_ref:rotate(omath.quat_from_euler_xyz(angle * 0.5, -angle, 0.0), true)
  end
end

function script.on_scene_mutation(ctx, dt_seconds)
  local _ = dt_seconds
  if not orb_toggle_requested then
    return
  end
  local host_node = node_ref
  if not valid(host_node) then
    host_node = resolve_host_node(ctx, false)
  end
  if not valid(host_node) then
    return
  end
  toggle_orb(host_node)
  orb_toggle_requested = false
end

return script
