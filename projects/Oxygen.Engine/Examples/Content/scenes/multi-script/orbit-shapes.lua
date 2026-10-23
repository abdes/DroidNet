local script = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1.6, min = 0, unit = "m/s",
      description = "Orbit speed" },
    bounce_amplitude = oxygen.param.float { default = 0.45, min = 0,
      unit = "m", description = "Height of the orbs' bounce" },
    host_material = oxygen.param.asset { kind = "material",
      description = "Material of the host shape" },
    orb_material = oxygen.param.asset { kind = "material",
      description = "Material of the orbiting shapes" },
    speed_up = oxygen.param.asset { kind = "input_action",
      description = "Action that speeds the orbit up" },
    speed_down = oxygen.param.asset { kind = "input_action",
      description = "Action that slows the orbit down" },
    stop = oxygen.param.asset { kind = "input_action",
      description = "Action that stops the motion" },
    invert = oxygen.param.asset { kind = "input_action",
      description = "Action that reverses the rotation" },
    cycle_shape = oxygen.param.asset { kind = "input_action",
      description = "Action that switches to the next shape" },
    spawn_orb = oxygen.param.asset { kind = "input_action",
      description = "Action that spawns or removes the companion orb" },
    pulse_scale = oxygen.param.asset { kind = "input_action",
      description = "Action that pulses the host's scale" },
  },
}

local scene = oxygen.scene
local assets = oxygen.assets
local input = oxygen.input
local otime = oxygen.time
local log = oxygen.log
local omath = oxygen.math

local kPulseSeconds = 0.22
local shapes = { "cube", "sphere", "cone", "torus", "cylinder", "quad" }

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

-- Binds `material` to the only surface of `node`'s procedural geometry.
local function set_surface_material(node, material)
  local surfaces = node:renderable_get_surface_ids()
  return surfaces ~= nil
    and node:renderable_set_material_override(surfaces[1], material)
end

local function apply_shape(self)
  local shape = shapes[self.shape_index]
  local geometry = assets.create_procedural_geometry(shape)
  if geometry == nil then
    log.error("showcase: failed to create geometry for " .. shape)
    return
  end
  if not self.node:renderable_set_geometry(geometry) then
    log.error("showcase: renderable_set_geometry failed for " .. shape)
  end
end

-- Applies `material` to `node` now, or once it has loaded.
local function apply_material(self, node, material)
  if material == nil or not valid(node) then
    return
  end
  local loaded = assets.get_material(material)
  if loaded ~= nil then
    set_surface_material(node, loaded)
    return
  end
  if assets.has_material(material) or self.pending_materials[material] then
    return
  end
  self.pending_materials[material] = true
  assets.load_material_async(material, function(mat)
    self.pending_materials[material] = nil
    if mat == nil then
      log.debug("showcase: material load miss/fail " .. material)
    elseif valid(node) then
      set_surface_material(node, mat)
    end
  end)
end

local function toggle_orb(self)
  if valid(self.orb) then
    scene.destroy_node(self.orb)
    self.orb = nil
    log.info("showcase: companion orb destroyed")
    return
  end

  self.orb = scene.create_node("CompanionOrb", self.node)
  if not valid(self.orb) then
    log.error("showcase: failed to create companion orb")
    return
  end

  self.orb:set_local_position(omath.vec3(2.0, 0.5, 0.0))
  local orb_geo = assets.create_procedural_geometry("sphere", {
    latitude_segments = 12,
    longitude_segments = 16,
  })
  if orb_geo ~= nil then
    self.orb:renderable_set_geometry(orb_geo)
  end
  local orb_mat = assets.create_debug_material()
  if orb_mat ~= nil then
    set_surface_material(self.orb, orb_mat)
  end
  apply_material(self, self.orb, self.params.orb_material)
  log.info("showcase: companion orb spawned orb=" .. node_tag(self.orb)
    .. " main=" .. node_tag(self.node))
end

local function on_triggered(action, handler)
  if action ~= nil then
    input.on_action(action, input.edges.triggered, handler)
  end
end

local function wire_input(self)
  local params = self.params
  on_triggered(params.speed_up, function()
    self.moving = true
    self.speed = self.speed + 0.25
    log.debug(string.format("showcase speed=%.2f", self.speed))
  end)
  on_triggered(params.speed_down, function()
    self.moving = true
    self.speed = math.max(0.0, self.speed - 0.25)
    log.debug(string.format("showcase speed=%.2f", self.speed))
  end)
  on_triggered(params.stop, function()
    self.moving = false
    log.debug("showcase movement stopped")
  end)
  on_triggered(params.invert, function()
    self.direction = -self.direction
    log.info(self.direction > 0.0 and "showcase direction: clockwise"
      or "showcase direction: counter-clockwise")
  end)
  on_triggered(params.cycle_shape, function()
    self.shape_index = (self.shape_index % #shapes) + 1
    apply_shape(self)
    log.info("showcase shape: " .. shapes[self.shape_index])
  end)
  on_triggered(params.spawn_orb, function()
    self.orb_toggle_requested = true
  end)
  on_triggered(params.pulse_scale, function()
    self.pulse_timer = kPulseSeconds
    log.debug("showcase pulse")
  end)
end

local function pulse_scale(self, dt)
  local s = 1.0
  if self.pulse_timer > 0.0 then
    self.pulse_timer = self.pulse_timer - dt
    local t = self.pulse_timer / kPulseSeconds
    s = 1.0 + math.sin((1.0 - t) * 12.0) * 0.12
  end
  self.node:set_local_scale(omath.vec3(s, s, s))
end

function script.on_start(self)
  self.speed = self.params.speed
  self.direction = 1.0
  self.moving = true
  self.bounce_phase = 0.0
  self.orb_phase = 0.0
  self.pulse_timer = 0.0
  self.shape_index = 1
  self.orb = nil
  self.orb_toggle_requested = false
  self.pending_materials = {}

  log.info("showcase: host node -> " .. node_tag(self.node)
    .. " assets.available=" .. tostring(assets.available())
    .. " assets.enabled=" .. tostring(assets.enabled()))
  if not self.node:has_renderable() then
    apply_shape(self)
  end
  apply_material(self, self.node, self.params.host_material)
  wire_input(self)
end

function script.on_gameplay(self, dt_seconds)
  local dt = otime.delta_seconds and otime.delta_seconds() or dt_seconds
  pulse_scale(self, dt)
  if not self.moving then
    return
  end

  local angle = dt * self.speed * self.direction
  if angle ~= 0.0 then
    self.node:rotate(omath.quat_from_euler_xyz(0.0, angle, angle * 0.2), true)
  end

  self.bounce_phase = self.bounce_phase + dt * (1.25 + self.speed * 0.2)
  local y = math.sin(self.bounce_phase) * self.params.bounce_amplitude
  self.node:set_local_position(omath.vec3(0.0, y, 0.0))

  if valid(self.orb) then
    self.orb_phase = self.orb_phase + dt * 1.8
    local radius = 1.8
    local ox = math.cos(self.orb_phase) * radius
    local oz = math.sin(self.orb_phase) * radius
    self.orb:set_local_position(omath.vec3(ox, 0.6, oz))
    self.orb:rotate(omath.quat_from_euler_xyz(angle * 0.5, -angle, 0.0), true)
  end
end

function script.on_scene_mutation(self, _dt_seconds)
  if self.orb_toggle_requested then
    self.orb_toggle_requested = false
    toggle_orb(self)
  end
end

return script
