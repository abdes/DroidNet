-- Shows the LOD groups the importer made of a model that states them with MSFT_lod
-- (left, blue lane) and of the same levels as named siblings (right, violet lane). An instance rides
-- each lane, away from the camera and back, and takes the colour of the LOD it
-- draws: green LOD 1, yellow LOD 2, red LOD 3. Beside each lane stand one instance
-- fixed to each LOD, in the same colours.
--
-- The first frames check it through the renderable API: three LODs that bind the
-- file's two materials, each fixed policy drawing its LOD, and the imported policy
-- following the distance. Results are logged as `[model-lods] PASS|FAIL`.

local script = oxygen.script {
  params = {
    msft_lod1 = oxygen.param.node { description = "msft lod1" },
    msft_lod2 = oxygen.param.node { description = "msft lod2" },
    msft_lod3 = oxygen.param.node { description = "msft lod3" },
    msft_default = oxygen.param.node { description = "msft default" },
    msft_mat1 = oxygen.param.asset { kind = "material" },
    msft_mat2 = oxygen.param.asset { kind = "material" },
    named_lod1 = oxygen.param.node { description = "named lod1" },
    named_lod2 = oxygen.param.node { description = "named lod2" },
    named_lod3 = oxygen.param.node { description = "named lod3" },
    named_default = oxygen.param.node { description = "named default" },
    named_mat1 = oxygen.param.asset { kind = "material" },
    named_mat2 = oxygen.param.asset { kind = "material" },
    real_default = oxygen.param.node { description = "the real file" },
    msft_probe = oxygen.param.node { description = "out of view, for the LOD check" },
    named_probe = oxygen.param.node { description = "out of view, for the LOD check" },
    real_mat1 = oxygen.param.asset { kind = "material" },
    real_mat2 = oxygen.param.asset { kind = "material" },
    color_lod1 = oxygen.param.asset { kind = "material" },
    color_lod2 = oxygen.param.asset { kind = "material" },
    color_lod3 = oxygen.param.asset { kind = "material" },
  },
}

local assets = oxygen.assets
local log = oxygen.log
local omath = oxygen.math
local scene = oxygen.scene
local time = oxygen.time

local TAG = "[model-lods] "
local LABELS = { "msft", "named" }
local CHECKED_LABELS = { "msft", "named", "real" }
local LANE_X = { msft = -6.0, named = 6.0 }
local LOD_COUNT = 3
local LOD_CHECK_FRAME = 30

-- The LOD follows the size on screen, which also depends on the window and on where
-- the camera is (RenderScene restores it per scene). So the check moves away from the
-- live camera in small steps, and asks for each LOD in turn rather than one at a
-- given distance. A LOD spans a distance ratio of 2, so no step can jump over one.
local SWEEP_NEAR = 5.0
local SWEEP_FAR = 300.0
local SWEEP_GROWTH = 1.15
local FRAMES_PER_STEP = 4

-- The ride along the lane, in meters from the scene's origin.
local RIDE_NEAR_Y = -13.0
local RIDE_FAR_Y = 62.0
local RIDE_HOVER = 1.0
local RIDE_PERIOD_SECONDS = 16.0
local SPIN_RADIANS_PER_SECOND = 0.6

local passed = 0
local failed = 0

local function check(ok, what)
  if ok then
    passed = passed + 1
    log.info(TAG .. "PASS " .. what)
  else
    failed = failed + 1
    log.error(TAG .. "FAIL " .. what)
  end
end

local function guid_of(value)
  if value == nil or type(value) == "string" then
    return value
  end
  return value:guid()
end

local function fix_policies(p)
  for _, label in ipairs(LABELS) do
    for lod = 1, LOD_COUNT do
      local name = label .. "_lod" .. lod
      check(p[name]:renderable_set_lod_policy_override(
        { kind = "fixed", index = lod }), "fix " .. name)
    end
  end
end

-- Before any colour override: what the file's materials bind.
local function check_bindings(p)
  for _, label in ipairs(CHECKED_LABELS) do
    local node = p[label .. "_default"]
    for lod = 1, LOD_COUNT do
      for submesh, material in ipairs({ "mat1", "mat2" }) do
        local expected = guid_of(assets.get_material(p[label .. "_" .. material]))
        check(expected ~= nil
            and guid_of(node:renderable_resolve_submesh_material(lod, submesh))
              == expected,
          string.format("%s LOD %d submesh %d binds %s", label, lod, submesh,
            material))
      end
    end
    check(node:renderable_get_lod_policy().kind == "screen_space_error",
      label .. " has the imported LOD policy")
  end
end

local function check_selection(p)
  for _, label in ipairs(LABELS) do
    for lod = 1, LOD_COUNT do
      local name = label .. "_lod" .. lod
      check(p[name]:renderable_get_active_lod_index() == lod,
        name .. " draws LOD " .. lod)
    end
  end
end

-- Behind the camera, which looks along +Y: a LOD is selected before the frustum
-- culls, so the check runs out of view.
local function place_away_from_camera(node, distance)
  local camera = scene.active_camera():get_world_position()
  node:set_local_position(omath.vec3(camera.x, camera.y - distance, camera.z))
end

local function record_lods(self)
  for _, label in ipairs(LABELS) do
    local lod = self.params[label .. "_probe"]:renderable_get_active_lod_index()
    local seen = self.lods_seen[label]
    if lod ~= nil and lod ~= seen[#seen] then
      seen[#seen + 1] = lod
    end
  end
end

-- Moves the probes one step further and records the LOD each one draws.
-- Returns true when the check is done.
local function step_distance_check(self)
  if self.sweep_distance == nil then
    self.sweep_distance = SWEEP_NEAR
  else
    record_lods(self)
    self.sweep_distance = self.sweep_distance * SWEEP_GROWTH
  end
  if self.sweep_distance > SWEEP_FAR then
    for _, label in ipairs(LABELS) do
      local seen = table.concat(self.lods_seen[label], ", ")
      check(seen == "1, 2, 3", string.format(
        "%s_probe draws LODs 1, 2, 3 in turn going away, drew %s", label, seen))
    end
    log.info(string.format(TAG .. "%d checks passed, %d failed", passed, failed))
    return true
  end
  for _, label in ipairs(LABELS) do
    place_away_from_camera(self.params[label .. "_probe"], self.sweep_distance)
  end
  return false
end

-- Eases from the near end to the far end of the lane and back.
local function ride(self, dt)
  self.ride_seconds = self.ride_seconds + dt
  local phase = (self.ride_seconds / RIDE_PERIOD_SECONDS) * math.pi * 2.0
  local along = 0.5 - (0.5 * math.cos(phase))
  local y = RIDE_NEAR_Y + ((RIDE_FAR_Y - RIDE_NEAR_Y) * along)
  local spin = omath.quat_from_euler_xyz(0.0, 0.0,
    self.ride_seconds * SPIN_RADIANS_PER_SECOND)
  for _, label in ipairs(LABELS) do
    local node = self.params[label .. "_default"]
    node:set_local_position(omath.vec3(LANE_X[label], y, RIDE_HOVER))
    node:set_local_rotation(spin)
  end
end

-- Gives an instance the colour of the LOD it draws, on every surface.
local function show_lod(self, name)
  local node = self.params[name]
  local lod = node:renderable_get_active_lod_index()
  if lod == nil or self.shown[name] == lod then
    return
  end
  self.shown[name] = lod
  local color = assets.get_material(self.params["color_lod" .. lod])
  for _, surface in ipairs(node:renderable_get_surface_ids() or {}) do
    node:renderable_set_material_override(surface, color)
  end
end

function script.on_start(self)
  self.started = false
  self.frames = 0
  self.sweep_distance = nil
  self.lods_seen = { msft = {}, named = {} }
  self.checks_done = false
  self.ride_seconds = 0.0
  self.shown = {}
  log.info(TAG .. "left, blue lane: MSFT_lod; right, violet lane: named "
    .. "siblings; green LOD 1, yellow LOD 2, red LOD 3")
end

function script.on_scene_mutation(self, dt_seconds)
  if not self.started then
    self.started = true
    check_bindings(self.params)
    fix_policies(self.params)
  end
  if not self.checks_done and self.frames >= LOD_CHECK_FRAME
    and self.frames % FRAMES_PER_STEP == 0 then
    self.checks_done = step_distance_check(self)
  end
  ride(self, dt_seconds)
  for _, label in ipairs(LABELS) do
    show_lod(self, label .. "_default")
    for lod = 1, LOD_COUNT do
      show_lod(self, label .. "_lod" .. lod)
    end
  end
end

function script.on_gameplay(self, _dt_seconds)
  self.frames = self.frames + 1
  if self.frames == LOD_CHECK_FRAME then
    check_selection(self.params)
  end
end

return script
