-- Applies the runtime surface and LOD overrides of the SurfaceOverrides scene,
-- checks every instance through the renderable API (the scene-authored rows
-- prove the cooked overrides reached the renderables), and drives the
-- playground row from the keyboard. Results are logged as `[surface-overrides] PASS|FAIL`.

local script = oxygen.script {
  params = {
    lamp_asset = oxygen.param.node { description = "Lamp with its asset defaults" },
    lamp_scene_material = oxygen.param.node {
      description = "Lamp whose shade material the scene overrides" },
    lamp_shade_hidden = oxygen.param.node {
      description = "Lamp whose shade the script hides" },
    lamp_metal_no_shadow = oxygen.param.node {
      description = "Lamp whose metal the script stops casting" },
    pot_asset = oxygen.param.node { description = "Pot with its asset defaults" },
    pot_scene_materials = oxygen.param.node {
      description = "Pot whose two materials the scene overrides" },
    pot_runtime = oxygen.param.node {
      description = "Pot the script recolours and partly hides" },
    pillar_asset = oxygen.param.node {
      description = "Pillar whose asset casts no shadow" },
    pillar_cast_override = oxygen.param.node {
      description = "Pillar the script makes cast" },
    ghost_asset = oxygen.param.node { description = "Ghost hidden by its asset" },
    ghost_visible_override = oxygen.param.node {
      description = "Ghost the script shows" },
    lod_asset = oxygen.param.node {
      description = "LOD chain with its asset distance policy" },
    lod_fixed = oxygen.param.node {
      description = "LOD chain fixed to its coarsest LOD" },
    lod_sse = oxygen.param.node {
      description = "LOD chain on a screen-space error policy" },
    lod_material = oxygen.param.node {
      description = "LOD chain with one material on every LOD" },
    lamp_scene_shade_hidden = oxygen.param.node {
      description = "Lamp whose shade the scene hides" },
    lamp_scene_metal_no_shadow = oxygen.param.node {
      description = "Lamp whose metal the scene stops casting" },
    pillar_scene_cast = oxygen.param.node {
      description = "Pillar the scene makes cast" },
    ghost_scene_visible = oxygen.param.node {
      description = "Ghost the scene shows" },
    lod_scene_fixed = oxygen.param.node {
      description = "LOD chain the scene fixes to its coarsest LOD" },
    lod_scene_sse = oxygen.param.node {
      description = "LOD chain on the scene's screen-space error policy" },
    play_lamp = oxygen.param.node { description = "Keyboard-driven lamp" },
    play_lod = oxygen.param.node { description = "Keyboard-driven LOD chain" },
    shade_red = oxygen.param.asset { kind = "material" },
    shade_blue = oxygen.param.asset { kind = "material" },
    pot_teal = oxygen.param.asset { kind = "material" },
    pot_gold = oxygen.param.asset { kind = "material" },
    lod_purple = oxygen.param.asset { kind = "material" },
    toggle_shade = oxygen.param.asset { kind = "input_action",
      description = "Shows or hides the playground lamp's shade" },
    toggle_metal_shadow = oxygen.param.asset { kind = "input_action",
      description = "Toggles the playground lamp's metal shadow" },
    cycle_shade_material = oxygen.param.asset { kind = "input_action",
      description = "Cycles the playground lamp's shade material" },
    cycle_lod_policy = oxygen.param.asset { kind = "input_action",
      description = "Cycles the playground LOD chain's policy" },
    reset_overrides = oxygen.param.asset { kind = "input_action",
      description = "Clears every playground override" },
  },
}

local assets = oxygen.assets
local input = oxygen.input
local log = oxygen.log

local TAG = "[surface-overrides] "

-- Surfaces of the cooked geometries, as their inventories declare them.
local LAMP_METAL = "d7f5a78e-b92f-4d10-b287-5ca6ba9428b8"
local LAMP_SHADE = "914071b0-c22f-f08b-ea6d-33dd2cec5584"
local POT_LIGHT = "15f61e58-02db-1303-d223-663d1e95110e"
local POT_DARK = "5aac8d25-d9b8-dafb-0dfb-da32cd6051c1"
local PILLAR = "48bdfd22-1873-48b5-8ae6-ef8495a77914"
local GHOST = "c55f1e5d-2d5e-4de8-8b90-c397e355db55"
local LOD_SURFACE = "cd681955-052b-4034-8b6f-0da3d3346b2a"

-- Submesh (1-based) of each surface in LOD 1.
local METAL_SUBMESH = 1
local SHADE_SUBMESH = 2
local LIGHT_SUBMESH = 1
local DARK_SUBMESH = 2

local SSE_POLICY = {
  kind = "screen_space_error",
  enter_finer_sse = { 120, 40 },
  exit_coarser_sse = { 100, 30 },
}
local PLAY_POLICIES = {
  { name = "asset default (distance)" },
  { name = "fixed LOD 1", policy = { kind = "fixed", index = 1 } },
  { name = "fixed LOD 2", policy = { kind = "fixed", index = 2 } },
  { name = "fixed LOD 3", policy = { kind = "fixed", index = 3 } },
  { name = "screen-space error", policy = SSE_POLICY },
}
local LOD_CHECK_FRAME = 30

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

local function material(param)
  if param == nil then
    return nil
  end
  return assets.get_material(param)
end

local function guid_of(value)
  if value == nil or type(value) == "string" then
    return value
  end
  return value:guid()
end

local function resolved_guid(node, lod, submesh)
  return guid_of(node:renderable_resolve_submesh_material(lod, submesh))
end

local function expect_surface(name, node, submesh, visible, casts)
  check(node:renderable_is_submesh_visible(1, submesh) == visible,
    string.format("%s submesh %d visible=%s", name, submesh, tostring(visible)))
  check(node:renderable_submesh_casts_shadows(1, submesh) == casts,
    string.format("%s submesh %d casts_shadows=%s", name, submesh,
      tostring(casts)))
end

local function expect_material(name, node, lod, submesh, mat)
  local expected = guid_of(mat)
  check(expected ~= nil and resolved_guid(node, lod, submesh) == expected,
    string.format("%s LOD %d submesh %d uses its override material", name, lod,
      submesh))
end

local function apply(ok, what)
  if not ok then
    failed = failed + 1
    log.error(TAG .. "FAIL could not " .. what)
  end
end

-- Static rows ----------------------------------------------------------------

local function apply_static_overrides(p)
  apply(p.lamp_shade_hidden:renderable_set_visibility_override(LAMP_SHADE,
    false), "hide the lamp shade")
  apply(p.lamp_metal_no_shadow:renderable_set_casts_shadows_override(
    LAMP_METAL, false), "stop the lamp metal casting")
  apply(p.pot_runtime:renderable_set_material_override(POT_DARK,
    material(p.pot_teal)), "recolour the pot's dark wood")
  apply(p.pot_runtime:renderable_set_visibility_override(POT_LIGHT, false),
    "hide the pot's light wood")
  apply(p.pillar_cast_override:renderable_set_casts_shadows_override(PILLAR,
    true), "make the pillar cast")
  apply(p.ghost_visible_override:renderable_set_visibility_override(GHOST,
    true), "show the ghost")
  apply(p.lod_fixed:renderable_set_lod_policy_override(
    { kind = "fixed", index = 3 }), "fix the LOD chain to LOD 3")
  apply(p.lod_sse:renderable_set_lod_policy_override(SSE_POLICY),
    "set the screen-space error policy")
  apply(p.lod_material:renderable_set_material_override(LOD_SURFACE,
    material(p.lod_purple)), "recolour every LOD")
end

local function validate_static(p)
  expect_surface("Lamp_Asset metal", p.lamp_asset, METAL_SUBMESH, true, true)
  expect_surface("Lamp_Asset shade", p.lamp_asset, SHADE_SUBMESH, true, true)
  check(resolved_guid(p.lamp_asset, 1, SHADE_SUBMESH)
      ~= guid_of(material(p.shade_red)),
    "Lamp_Asset shade keeps its asset material")
  expect_material("Lamp_SceneMaterial shade", p.lamp_scene_material, 1,
    SHADE_SUBMESH, material(p.shade_red))
  expect_surface("Lamp_ShadeHidden shade", p.lamp_shade_hidden, SHADE_SUBMESH,
    false, false)
  expect_surface("Lamp_ShadeHidden metal", p.lamp_shade_hidden, METAL_SUBMESH,
    true, true)
  expect_surface("Lamp_MetalNoShadow metal", p.lamp_metal_no_shadow,
    METAL_SUBMESH, true, false)
  expect_surface("Lamp_MetalNoShadow shade", p.lamp_metal_no_shadow,
    SHADE_SUBMESH, true, true)

  check(resolved_guid(p.pot_asset, 1, LIGHT_SUBMESH)
      ~= guid_of(material(p.pot_teal)),
    "Pot_Asset light wood keeps its asset material")
  expect_material("Pot_SceneMaterials light wood", p.pot_scene_materials, 1,
    LIGHT_SUBMESH, material(p.pot_teal))
  expect_material("Pot_SceneMaterials dark wood", p.pot_scene_materials, 1,
    DARK_SUBMESH, material(p.pot_gold))
  expect_material("Pot_Runtime dark wood", p.pot_runtime, 1, DARK_SUBMESH,
    material(p.pot_teal))
  expect_surface("Pot_Runtime light wood", p.pot_runtime, LIGHT_SUBMESH, false,
    false)

  expect_surface("Pillar_Asset (asset: no shadow)", p.pillar_asset, 1, true,
    false)
  expect_surface("Pillar_CastOverride", p.pillar_cast_override, 1, true, true)
  expect_surface("Ghost_Asset (asset: hidden)", p.ghost_asset, 1, false, false)
  expect_surface("Ghost_VisibleOverride", p.ghost_visible_override, 1, true,
    true)

  local asset_policy = p.lod_asset:renderable_get_lod_policy()
  check(asset_policy ~= nil and asset_policy.kind == "distance"
      and #asset_policy.thresholds == 2 and asset_policy.thresholds[1] == 14,
    "Lod_Asset uses its asset distance policy")
  local fixed = p.lod_fixed:renderable_get_lod_policy()
  check(fixed ~= nil and fixed.kind == "fixed" and fixed.index == 3,
    "Lod_Fixed2 policy is fixed LOD 3")
  local sse = p.lod_sse:renderable_get_lod_policy()
  check(sse ~= nil and sse.kind == "screen_space_error"
      and sse.enter_finer_sse[1] == 120,
    "Lod_ScreenSpaceError policy is the override")
  for lod = 1, 3 do
    expect_material("Lod_Material", p.lod_material, lod, 1,
      material(p.lod_purple))
  end
end

-- The same overrides as the static rows, authored in the scene: they arrive
-- through the cooked override table, before the script runs.
local function validate_scene_authored(p)
  expect_surface("Lamp_SceneShadeHidden shade", p.lamp_scene_shade_hidden,
    SHADE_SUBMESH, false, false)
  expect_surface("Lamp_SceneShadeHidden metal", p.lamp_scene_shade_hidden,
    METAL_SUBMESH, true, true)
  expect_surface("Lamp_SceneMetalNoShadow metal", p.lamp_scene_metal_no_shadow,
    METAL_SUBMESH, true, false)
  expect_surface("Pillar_SceneCast", p.pillar_scene_cast, 1, true, true)
  expect_surface("Ghost_SceneVisible", p.ghost_scene_visible, 1, true, true)
  local fixed = p.lod_scene_fixed:renderable_get_lod_policy()
  check(fixed ~= nil and fixed.kind == "fixed" and fixed.index == 3,
    "Lod_SceneFixed policy is the scene's fixed LOD 3")
  local sse = p.lod_scene_sse:renderable_get_lod_policy()
  check(sse ~= nil and sse.kind == "screen_space_error"
      and sse.enter_finer_sse[1] == 120 and sse.exit_coarser_sse[2] == 30,
    "Lod_SceneScreenSpaceError policy is the scene's")
end

local function validate_lod_selection(p)
  check(p.lod_fixed:renderable_get_active_lod_index() == 3,
    "Lod_Fixed2 draws LOD 3")
  check(p.lod_scene_fixed:renderable_get_active_lod_index() == 3,
    "Lod_SceneFixed draws LOD 3")
  check(p.lod_asset:renderable_get_active_lod_index() ~= nil,
    "Lod_Asset selected a LOD by distance")
  check(p.lod_sse:renderable_get_active_lod_index() ~= nil,
    "Lod_ScreenSpaceError selected a LOD by screen-space error")
  log.info(string.format(TAG .. "%d checks passed, %d failed", passed, failed))
end

-- Playground -----------------------------------------------------------------

local function apply_playground(self)
  local p = self.params
  local lamp = p.play_lamp
  local state = self.play
  if state.shade_hidden then
    lamp:renderable_set_visibility_override(LAMP_SHADE, false)
  else
    lamp:renderable_clear_visibility_override(LAMP_SHADE)
  end
  if state.metal_no_shadow then
    lamp:renderable_set_casts_shadows_override(LAMP_METAL, false)
  else
    lamp:renderable_clear_casts_shadows_override(LAMP_METAL)
  end
  local shade_materials = { nil, p.shade_red, p.shade_blue }
  local shade = shade_materials[state.shade_material]
  if shade == nil then
    lamp:renderable_clear_material_override(LAMP_SHADE)
  else
    lamp:renderable_set_material_override(LAMP_SHADE, material(shade))
  end
  local entry = PLAY_POLICIES[state.lod_policy]
  if entry.policy == nil then
    p.play_lod:renderable_clear_lod_policy_override()
  else
    p.play_lod:renderable_set_lod_policy_override(entry.policy)
  end
  log.info(string.format(
    TAG .. "playground: shade %s, metal shadow %s, shade material %d, LOD %s",
    state.shade_hidden and "hidden" or "shown",
    state.metal_no_shadow and "off" or "on", state.shade_material, entry.name))
end

local function wire_input(self)
  local p = self.params
  local function on_pressed(action, handler)
    if action ~= nil then
      input.on_action(action, input.edges.triggered, function()
        handler(self.play)
        self.play_dirty = true
      end)
    end
  end
  on_pressed(p.toggle_shade, function(s) s.shade_hidden = not s.shade_hidden end)
  on_pressed(p.toggle_metal_shadow, function(s)
    s.metal_no_shadow = not s.metal_no_shadow
  end)
  on_pressed(p.cycle_shade_material, function(s)
    s.shade_material = s.shade_material % 3 + 1
  end)
  on_pressed(p.cycle_lod_policy, function(s)
    s.lod_policy = s.lod_policy % #PLAY_POLICIES + 1
  end)
  on_pressed(p.reset_overrides, function(s)
    s.shade_hidden = false
    s.metal_no_shadow = false
    s.shade_material = 1
    s.lod_policy = 1
  end)
end

function script.on_start(self)
  self.started = false
  self.frames = 0
  self.play = {
    shade_hidden = false,
    metal_no_shadow = false,
    shade_material = 1,
    lod_policy = 1,
  }
  self.play_dirty = false
  wire_input(self)
  log.info(TAG .. "keys: 1 shade, 2 metal shadow, 3 shade material, "
    .. "4 LOD policy, 0 reset")
end

function script.on_scene_mutation(self, _dt_seconds)
  if not self.started then
    self.started = true
    validate_scene_authored(self.params)
    apply_static_overrides(self.params)
    validate_static(self.params)
  end
  if self.play_dirty then
    self.play_dirty = false
    apply_playground(self)
  end
end

function script.on_gameplay(self, _dt_seconds)
  if self.frames < LOD_CHECK_FRAME then
    self.frames = self.frames + 1
    if self.frames == LOD_CHECK_FRAME then
      validate_lod_selection(self.params)
    end
  end
end

return script
