//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

#include <memory>
#include <string>

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::scripting::test {
class SceneBindingsTest : public ScriptingModuleTest { };

namespace {

  using oxygen::co::testing::TestEventLoop;

  auto RunSceneMutationPhase(
    ScriptingModule& module, observer_ptr<engine::FrameContext> context) -> void
  {
    TestEventLoop loop;
    oxygen::co::Run(
      loop, [&]() -> co::Co<> { co_await module.OnSceneMutation(context); });
  }

} // namespace

NOLINT_TEST_F(
  SceneBindingsTest, ExecuteScriptSceneBindingsExposeV1SceneModuleSurface)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local scene = oxygen.scene
if type(scene) ~= "table" then
  error("expected oxygen.scene table")
end
local expected = {
  "current_node", "param",
  "create_node", "destroy_node", "destroy_hierarchy", "reparent", "root_nodes",
  "find_one", "find_many", "count", "exists", "query",
  "has_environment", "get_environment", "ensure_environment", "clear_environment"
}
for i = 1, #expected do
  local key = expected[i]
  if type(scene[key]) ~= "function" then
    error("expected scene." .. key .. " function")
  end
end

)lua" },
    .chunk_name = ScriptChunkName { "scene_bindings_v1_surface" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(
  SceneBindingsTest, ExecuteScriptSceneBindingsDoNotExportLegacySceneNames)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local scene = oxygen.scene

local function assert_legacy_removed(key)
  local ok, value = pcall(function()
    return scene[key]
  end)
  if ok then
    if value ~= nil then
      error("legacy scene." .. key .. " must be absent")
    end
    return
  end
  if type(value) ~= "string" or string.find(value, "removed in v1", 1, true) == nil then
    error("legacy scene." .. key .. " unexpected error shape")
  end
end

assert_legacy_removed("find")
assert_legacy_removed("find_path")
assert_legacy_removed("create")
assert_legacy_removed("current")
assert_legacy_removed("get_param")
)lua" },
    .chunk_name = ScriptChunkName { "scene_bindings_legacy_absence" },
  });

  EXPECT_TRUE(result.ok) << result.message;
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneEnvironmentBindingsSupportSystemRoundtrip)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene
  local env = scene.ensure_environment()
  if env == nil then error("missing env") end

  local fog = env:ensure_fog()
  fog:set_model("exponential_height")
  if fog:get_model() ~= "exponential_height" then error("fog model") end
  fog:set_max_opacity(0.5)
  if math.abs(fog:get_max_opacity() - 0.5) > 0.0001 then error("fog opacity") end

  local sky_light = env:ensure_sky_light()
  sky_light:set({ source = "captured_scene", intensity_mul = 1.25 })
  local sl = sky_light:get()
  if sl.source ~= "captured_scene" then error("sky_light source") end
  if math.abs(sl.intensity_mul - 1.25) > 0.0001 then error("sky_light intensity") end

  local sky = env:ensure_sky_atmosphere()
  sky:set({ planet_radius_meters = 7000000.0 })
  local sa = sky:get()
  if math.abs(sa.planet_radius_meters - 7000000.0) > 0.0001 then error("sky_atmosphere radius") end

  local clouds = env:ensure_clouds()
  clouds:set({ coverage = 0.42 })
  local c = clouds:get()
  if math.abs(c.coverage - 0.42) > 0.0001 then error("cloud coverage") end

  local sun = env:ensure_sun()
  sun:set({ source = "synthetic", casts_shadows = false, illuminance_lx = 1000.0 })
  local s = sun:get()
  if s.source ~= "synthetic" then error("sun source") end
  if s.casts_shadows ~= false then error("sun shadows") end

  local pp = env:ensure_post_process()
  pp:set({ tone_mapper = "reinhard", exposure_mode = "manual", manual_exposure_ev = 8.0 })
  local p = pp:get()
  if p.tone_mapper ~= "reinhard" then error("tone mapper") end
  if p.exposure_mode ~= "manual" then error("exposure mode") end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_environment_roundtrip" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest, OnSceneMutationSceneNodeScriptingBindingsWork)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene
  local node = scene.create_node("ScriptHost", nil)
  if node == nil then error("node create failed") end
  if not node:attach_scripting() then error("attach_scripting failed") end
  if not node:has_scripting() then error("has_scripting false") end

  if node:scripting_slots_count() ~= 0 then error("slots initial mismatch") end
  if not node:scripting_add_slot("scripts/test_runtime_slot.luau") then
    error("scripting_add_slot failed")
  end
  if node:scripting_slots_count() ~= 1 then error("slots add mismatch") end

  local slots = node:scripting_slots()
  if #slots ~= 1 then error("slots view mismatch") end
  local slot = slots[1]
  if slot == nil then error("slot ref missing") end

  if not node:scripting_set_param(slot, "speed", 12.5) then
    error("set_param speed failed")
  end
  if not node:scripting_set_param(slot, "enabled", true) then
    error("set_param enabled failed")
  end
  local speed = node:scripting_get_param(slot, "speed")
  if type(speed) ~= "number" then error("speed type mismatch") end
  if math.abs(speed - 12.5) > 0.0001 then error("speed value mismatch") end
  local enabled = node:scripting_get_param(slot, "enabled")
  if enabled ~= true then error("enabled value mismatch") end

  if not node:scripting_remove_slot(slot) then error("remove slot failed") end
  if node:scripting_slots_count() ~= 0 then error("slots remove mismatch") end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_node_scripting_roundtrip" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  const auto errors = context.GetErrors();
  EXPECT_FALSE(context.HasErrors())
    << (errors.empty() ? std::string("unknown scripting error")
                       : errors.front().message);
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneNodeScriptingRejectsTamperedSlotReference)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene
  local node = scene.create_node("ScriptHostStale", nil)
  if node == nil then error("node create failed") end
  if not node:attach_scripting() then error("attach_scripting failed") end

  if not node:scripting_add_slot("scripts/slot_a.luau") then
    error("add slot a failed")
  end
  local slots_a = node:scripting_slots()
  local slot = slots_a[1]
  if slot == nil then error("missing slot") end

  local tampered = { index = slot.index, __slot_ptr = nil }
  if node:scripting_set_param(tampered, "x", 1.0) then
    error("tampered slot unexpectedly accepted")
  end
  if node:scripting_get_param(tampered, "x") ~= nil then
    error("tampered slot get should return nil")
  end
  local params = node:scripting_params(tampered)
  if type(params) ~= "table" or #params ~= 0 then
    error("tampered slot params should be empty table")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_node_scripting_tampered_slot" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneRenderableBindingsGeometryMaterialMarshallingWorks)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene
  local node = scene.create_node("RenderableHost", nil)
  if node == nil then error("node create failed") end

  if not node:renderable_set_geometry("geo://cube_v1") then
    error("set geometry failed")
  end
  if not node:has_renderable() then error("has_renderable false") end

  local geometry = node:renderable_get_geometry()
  if geometry ~= "geo://cube_v1" then error("geometry token mismatch") end

  if not node:renderable_set_material_override(1, 1, "mat://override_v1") then
    error("set material override failed")
  end

  local resolved_override = node:renderable_resolve_submesh_material(1, 1)
  if resolved_override ~= "mat://override_v1" then
    error("resolve override mismatch")
  end

  if not node:renderable_clear_material_override(1, 1) then
    error("clear material override failed")
  end

  local resolved_default = node:renderable_resolve_submesh_material(1, 1)
  if resolved_default == nil then error("resolved default material missing") end

  local call_ok_bool, ok_bool = pcall(function()
    return node:renderable_set_all_submeshes_visible(1)
  end)
  if not call_ok_bool then
    error("set_all_submeshes_visible must not throw on invalid type")
  end
  if ok_bool then
    error("expected boolean type check for set_all_submeshes_visible")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_renderable_marshalling_roundtrip" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneRenderableBindingsAcceptAssetsUserdataHandles)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local assets = oxygen.assets
  local scene = oxygen.scene
  local node = scene.create_node("RenderableUserdataHost", nil)
  if node == nil then error("node create failed") end

  local geometry = assets.create_procedural_geometry("cube")
  if geometry == nil then error("procedural geometry missing") end
  if geometry:type_name() ~= "GeometryAsset" then error("unexpected geometry type") end
  if not node:renderable_set_geometry(geometry) then
    error("set geometry userdata failed")
  end

  local got_geometry = node:renderable_get_geometry()
  if got_geometry == nil then error("get geometry userdata missing") end
  if got_geometry:type_name() ~= "GeometryAsset" then
    error("get geometry userdata type mismatch")
  end

  local material = assets.create_default_material()
  if material == nil then error("default material missing") end
  if material:type_name() ~= "MaterialAsset" then error("unexpected material type") end
  if not node:renderable_set_material_override(1, 1, material) then
    error("set material userdata failed")
  end

  local resolved = node:renderable_resolve_submesh_material(1, 1)
  if resolved == nil then error("resolved material userdata missing") end
  if resolved:type_name() ~= "MaterialAsset" then
    error("resolved material userdata type mismatch")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_renderable_userdata_handles" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneRenderableBindingsRejectWrongAssetsUserdataKinds)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local assets = oxygen.assets
  local scene = oxygen.scene
  local node = scene.create_node("RenderableWrongUserdataKinds", nil)
  if node == nil then error("node create failed") end

  local geometry = assets.create_procedural_geometry("cube")
  local material = assets.create_default_material()
  if geometry == nil or material == nil then
    error("procedural assets missing")
  end

  local ok_set_geometry, result_set_geometry = pcall(function()
    return node:renderable_set_geometry(material)
  end)
  if ok_set_geometry and result_set_geometry ~= false and result_set_geometry ~= nil then
    error("renderable_set_geometry should reject MaterialAsset userdata deterministically")
  end

  if not node:renderable_set_geometry(geometry) then
    error("renderable_set_geometry with GeometryAsset failed")
  end

  local ok_set_material, result_set_material = pcall(function()
    return node:renderable_set_material_override(1, 1, geometry)
  end)
  if ok_set_material and result_set_material ~= false and result_set_material ~= nil then
    error("renderable_set_material_override should reject GeometryAsset userdata deterministically")
  end
end
)lua" },
    .chunk_name
    = ScriptChunkName { "scene_renderable_userdata_kind_rejection" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneQueryUserdataReturnsDeterministicDefaultsAfterSceneExpiry)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
phase = 0
q = nil

function on_scene_mutation()
  local scene = oxygen.scene
  phase = phase + 1
  if phase == 1 then
    scene.create_node("QNode", nil)
    q = scene.query("**")
    if q == nil then error("query userdata expected") end
    return
  end

  if phase == 2 then
    if q:first() ~= nil then error("expired query first must be nil") end
    local all = q:all()
    if type(all) ~= "table" or #all ~= 0 then
      error("expired query all must be empty table")
    end
    if q:count() ~= 0 then error("expired query count must be 0") end
    if q:any() ~= false then error("expired query any must be false") end
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_query_expiry_defaults" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene_a
    = std::make_shared<scene::Scene>("scene_a", kDefaultSceneCapacity);
  engine::FrameContext frame_a;
  const auto tag = engine::internal::EngineTagFactory::Get();
  frame_a.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  frame_a.SetScene(observer_ptr<scene::Scene> { scene_a.get() });
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &frame_a });
  if (frame_a.HasErrors()) {
    const auto errors = frame_a.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }

  scene_a.reset();

  auto scene_b
    = std::make_shared<scene::Scene>("scene_b", kDefaultSceneCapacity);
  engine::FrameContext frame_b;
  frame_b.SetFrameSequenceNumber(frame::SequenceNumber { 2 }, tag);
  frame_b.SetScene(observer_ptr<scene::Scene> { scene_b.get() });
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &frame_b });
  if (frame_b.HasErrors()) {
    const auto errors = frame_b.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneComponentBindingsRejectInvalidArgumentShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene

  local light_node = scene.create_node("LightHostInvalidArgs", nil)
  if light_node == nil then error("light node create failed") end
  if not light_node:attach_directional_light({}) then
    error("attach directional light failed")
  end
  if light_node:light_set_affects_world(1) ~= false then
    error("light_set_affects_world should reject non-boolean")
  end

  local camera_node = scene.create_node("CameraHostInvalidArgs", nil)
  if camera_node == nil then error("camera node create failed") end
  if not camera_node:attach_perspective_camera({}) then
    error("attach camera failed")
  end
  if camera_node:camera_set_perspective(1) ~= false then
    error("camera_set_perspective should require table")
  end

  local script_node = scene.create_node("ScriptHostInvalidArgs", nil)
  if script_node == nil then error("script node create failed") end
  if not script_node:attach_scripting() then error("attach_scripting failed") end
  if not script_node:scripting_add_slot("scripts/invalid_args_slot.luau") then
    error("add slot failed")
  end
  local slot = script_node:scripting_slots()[1]
  if slot == nil then error("slot ref missing") end

  if script_node:scripting_set_param(slot, "unsupported", { 1, 2, 3, 4, 5 }) ~= false then
    error("scripting_set_param should reject unsupported table shape")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_component_invalid_args" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneScenarioHierarchyQueryComponentsEnvironment)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene

  local env = scene.ensure_environment()
  local fog = env:ensure_fog()
  fog:set_max_opacity(0.33)
  if math.abs(fog:get_max_opacity() - 0.33) > 0.0001 then
    error("fog roundtrip failed")
  end

  local world = scene.create_node("World", nil)
  local player = scene.create_node("Player", world)
  local enemy_a = scene.create_node("Enemy_A", world)
  local enemy_b = scene.create_node("Enemy_B", world)
  local light = scene.create_node("FillLight", world)
  if world == nil or player == nil or enemy_a == nil or enemy_b == nil or light == nil then
    error("node create failed")
  end

  if not light:attach_point_light({}) then
    error("attach point light failed")
  end
  if not light:light_set_luminous_flux_lm(2000.0) then
    error("set luminous flux failed")
  end
  if math.abs(light:light_get_luminous_flux_lm() - 2000.0) > 0.0001 then
    error("light luminous flux mismatch")
  end

  if not player:renderable_set_geometry("geo://player") then
    error("player set geometry failed")
  end
  if player:renderable_get_geometry() ~= "geo://player" then
    error("player geometry token mismatch")
  end

  local all = scene.query("**"):all()
  local enemy_count = 0
  for i = 1, #all do
    local name = all[i]:get_name()
    if string.sub(name, 1, 6) == "Enemy_" then
      enemy_count = enemy_count + 1
    end
  end
  if enemy_count ~= 2 then
    error("enemy count mismatch")
  end

  if not scene.reparent(enemy_a, player, true) then
    error("reparent failed")
  end
  local parent = enemy_a:get_parent()
  if parent == nil or parent ~= player then
    error("enemy parent mismatch after reparent")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_scenario_full_flow" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneNodeMetatableExposesRequiredComponentMethods)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene
  local node = scene.create_node("MethodSurfaceNode", nil)
  if node == nil then error("node create failed") end

  local expected = {
    "attach_perspective_camera", "attach_orthographic_camera", "detach_camera", "has_camera",
    "attach_directional_light", "attach_point_light", "attach_spot_light", "detach_light", "has_light",
    "renderable_set_geometry", "renderable_get_geometry", "renderable_set_material_override",
    "renderable_resolve_submesh_material", "renderable_set_all_submeshes_visible",
    "attach_scripting", "detach_scripting", "has_scripting", "scripting_slots",
    "scripting_add_slot", "scripting_remove_slot", "scripting_set_param", "scripting_get_param"
  }

  for i = 1, #expected do
    local key = expected[i]
    if type(node[key]) ~= "function" then
      error("missing node method: " .. key)
    end
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_node_method_surface_regression" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneQueryScopeAndClearScopeEdgeBehaviorWorks)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  local scene = oxygen.scene
  local root = scene.create_node("ScopeRoot", nil)
  local in_scope = scene.create_node("InScopeEnemy", root)
  local out_scope = scene.create_node("OutScopeEnemy", nil)
  if root == nil or in_scope == nil or out_scope == nil then
    error("scope setup failed")
  end

  local q = scene.query("**")
  q:scope(root)
  local scoped = q:all()
  local found_in_scope = false
  local found_out_scope = false
  for i = 1, #scoped do
    local name = scoped[i]:get_name()
    if name == "InScopeEnemy" then found_in_scope = true end
    if name == "OutScopeEnemy" then found_out_scope = true end
  end
  if not found_in_scope then error("scoped query missing in-scope node") end
  if found_out_scope then error("scoped query leaked out-of-scope node") end

  q:clear_scope()
  if q:count() < 3 then
    error("clear_scope should restore full-scene query visibility")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_query_scope_clear_scope_edges" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene = std::make_shared<scene::Scene>(
    "scene_binding_test", kDefaultSceneCapacity);
  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  context.SetScene(observer_ptr<scene::Scene> { scene.get() });

  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  if (context.HasErrors()) {
    const auto errors = context.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

NOLINT_TEST_F(SceneBindingsTest,
  OnSceneMutationSceneNodeHandlesFromExpiredSceneFailDeterministically)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
phase = 0
stale = nil

function on_scene_mutation()
  local scene = oxygen.scene
  phase = phase + 1
  if phase == 1 then
    stale = scene.create_node("WillExpire", nil)
    if stale == nil then error("failed to create stale candidate node") end
    return
  end

  if phase == 2 then
    if stale == nil then error("missing stale handle") end
    if stale:is_alive() ~= false then error("stale node should be dead") end
    local id = stale:runtime_id()
    if type(id) ~= "table" then error("stale runtime_id should be table") end
    if type(id.scene_id) ~= "number" then error("stale runtime_id.scene_id missing") end
    if type(id.node_index) ~= "number" then error("stale runtime_id.node_index missing") end
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "scene_stale_node_handle_determinism" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  auto scene_a
    = std::make_shared<scene::Scene>("scene_a", kDefaultSceneCapacity);
  engine::FrameContext frame_a;
  const auto tag = engine::internal::EngineTagFactory::Get();
  frame_a.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, tag);
  frame_a.SetScene(observer_ptr<scene::Scene> { scene_a.get() });
  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &frame_a });
  if (frame_a.HasErrors()) {
    const auto errors = frame_a.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }

  scene_a.reset();

  auto scene_b
    = std::make_shared<scene::Scene>("scene_b", kDefaultSceneCapacity);
  engine::FrameContext frame_b;
  frame_b.SetFrameSequenceNumber(frame::SequenceNumber { 2 }, tag);
  frame_b.SetScene(observer_ptr<scene::Scene> { scene_b.get() });
  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &frame_b });
  if (frame_b.HasErrors()) {
    const auto errors = frame_b.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }
}

} // namespace oxygen::scripting::test
