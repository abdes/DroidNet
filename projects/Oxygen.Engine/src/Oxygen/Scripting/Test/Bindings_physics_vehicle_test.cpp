//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakePhysicsSystem.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::scripting::test {

class PhysicsVehicleBindingsTest : public ScriptingModuleTest { };

using oxygen::co::testing::TestEventLoop;

namespace {
  auto RunGameplayPhase(ScriptingModule& module,
    physics::PhysicsModule& phys_module,
    observer_ptr<engine::FrameContext> context) -> void
  {
    context->SetCurrentPhase(
      core::PhaseId::kGameplay, engine::internal::EngineTagFactory::Get());
    TestEventLoop loop;
    oxygen::co::Run(loop, [&module, &phys_module, context]() -> co::Co<> {
      co_await phys_module.OnGameplay(context);
      co_await module.OnGameplay(context);
    });
  }

  auto RunSceneMutationPhase(ScriptingModule& module,
    physics::PhysicsModule& phys_module,
    observer_ptr<engine::FrameContext> context) -> void
  {
    context->SetCurrentPhase(
      core::PhaseId::kSceneMutation, engine::internal::EngineTagFactory::Get());
    TestEventLoop loop;
    oxygen::co::Run(loop, [&module, &phys_module, context]() -> co::Co<> {
      co_await phys_module.OnSceneMutation(context);
      co_await module.OnSceneMutation(context);
    });
  }

  auto RunFixedSimulationPhase(ScriptingModule& module,
    physics::PhysicsModule& phys_module,
    observer_ptr<engine::FrameContext> context) -> void
  {
    context->SetCurrentPhase(core::PhaseId::kFixedSimulation,
      engine::internal::EngineTagFactory::Get());
    TestEventLoop loop;
    oxygen::co::Run(loop, [&module, &phys_module, context]() -> co::Co<> {
      co_await phys_module.OnFixedSimulation(context);
      co_await module.OnFixedSimulation(context);
    });
  }
} // namespace

class PhysicsVehicleBindingsIntegrationTest : public ScriptingModuleTest {
protected:
  void SetUp() override
  {
    ScriptingModuleTest::SetUp();

    auto fake_system
      = std::make_unique<physics::test::detail::FakePhysicsSystem>();
    fake_system_ = fake_system.get();
    fake_system_->State().world_created = true;
    fake_system_->State().world_id = physics::WorldId { 1U };

    physics_module_ = std::make_unique<physics::PhysicsModule>(
      engine::kPhysicsModulePriority, std::move(fake_system));
    FakeEngine().AddModule(*physics_module_);
  }

  void TearDown() override
  {
    physics_module_.reset();
    fake_system_ = nullptr;
    ScriptingModuleTest::TearDown();
  }

  auto SetupTestContext() -> void
  {
    scene_ = std::make_shared<scene::Scene>(
      "vehicle_bindings_test", kDefaultSceneCapacity);
    context_.SetFrameSequenceNumber(frame::SequenceNumber { 1U }, Tag::Get());
    context_.SetScene(observer_ptr<scene::Scene> { scene_.get() });
  }

protected:
  std::shared_ptr<scene::Scene> scene_;
  engine::FrameContext context_;
  physics::test::detail::FakePhysicsSystem* fake_system_ { nullptr };
  std::unique_ptr<physics::PhysicsModule> physics_module_;
};

NOLINT_TEST_F(
  PhysicsVehicleBindingsTest, PhysicsVehicleBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local vehicle = oxygen.physics.vehicle
if type(vehicle.create) ~= "function" then error("missing vehicle.create") end
if type(vehicle.get_exact) ~= "function" then error("missing vehicle.get_exact") end
if type(vehicle.find_in_ancestors) ~= "function" then error("missing vehicle.find_in_ancestors") end
if type(vehicle.destroy) ~= "function" then error("missing vehicle.destroy") end
if type(vehicle.set_control_input) ~= "function" then error("missing vehicle.set_control_input") end
if type(vehicle.get_state) ~= "function" then error("missing vehicle.get_state") end
if type(vehicle.get_authority) ~= "function" then error("missing vehicle.get_authority") end
if type(vehicle.flush_structural_changes) ~= "function" then error("missing vehicle.flush_structural_changes") end

if vehicle.create({
  chassis_body_id = 1,
  wheels = {
    { body_id = 2, axle_index = 0, side = "left" },
    { body_id = 3, axle_index = 0, side = "right" },
  },
  constraint_settings_blob = "x",
}) ~= nil then
  error("vehicle.create should return nil without module")
end
if vehicle.flush_structural_changes() ~= nil then
  error("vehicle.flush_structural_changes should return nil without module")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_vehicle_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsVehicleBindingsTest, PhysicsVehicleBindingsRejectInvalidHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local vehicle = oxygen.physics.vehicle

local ok = pcall(function() vehicle.destroy(nil) end)
if ok then error("vehicle.destroy must reject invalid handle") end

if vehicle.get_exact(nil) ~= nil then
  error("vehicle.get_exact should return nil for invalid scene node")
end
if vehicle.find_in_ancestors(nil) ~= nil then
  error("vehicle.find_in_ancestors should return nil for invalid scene node")
end

ok = pcall(function() vehicle.set_control_input(nil, {}) end)
if ok then error("vehicle.set_control_input must reject invalid handle") end

local ok_unknown, err_unknown = pcall(function()
  vehicle.set_control_input(nil, { invalid_field = 1.0 })
end)
if ok_unknown then
  error("vehicle.set_control_input must reject unknown input fields")
end
if type(err_unknown) ~= "string" or string.find(err_unknown, "unknown vehicle input field 'invalid_field'", 1, true) == nil then
  error("vehicle.set_control_input must report unknown field name")
end

ok = pcall(function() vehicle.get_state(nil) end)
if ok then error("vehicle.get_state must reject invalid handle") end

ok = pcall(function() vehicle.get_authority(nil) end)
if ok then error("vehicle.get_authority must reject invalid handle") end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_vehicle_invalid_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(PhysicsVehicleBindingsIntegrationTest,
  VehicleSetControlInputIsGameplayOnlyAndPropagatesToBackend)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  SetupTestContext();

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  _G.chassis_node = oxygen.scene.create_node("VehicleChassis", nil)
  _G.wheel_a_node = oxygen.scene.create_node("VehicleWheelA", nil)
  _G.wheel_b_node = oxygen.scene.create_node("VehicleWheelB", nil)
end

function on_gameplay()
  if _G.vehicle_handle ~= nil then
    return
  end

  local body = oxygen.physics.body
  local vehicle = oxygen.physics.vehicle
  local chassis = body.attach(_G.chassis_node, { body_type = "dynamic" })
  local wheel_a = body.attach(_G.wheel_a_node, { body_type = "dynamic" })
  local wheel_b = body.attach(_G.wheel_b_node, { body_type = "dynamic" })
  if chassis == nil or wheel_a == nil or wheel_b == nil then
    error("failed to attach vehicle bodies")
  end

  _G.vehicle_handle = vehicle.create({
    chassis_body_id = chassis,
    wheels = {
      { body_id = wheel_a, axle_index = 0, side = "left" },
      { body_id = wheel_b, axle_index = 0, side = "right" },
    },
    constraint_settings_blob = "x",
  })
  if _G.vehicle_handle == nil then
    error("vehicle.create failed")
  end

  local ok = vehicle.set_control_input(_G.vehicle_handle, {
    forward = 0.65,
    brake = 0.0,
    steering = -0.15,
    hand_brake = 0.0,
  })
  if ok ~= true then
    error("vehicle.set_control_input should succeed in gameplay")
  end
end

function on_fixed_simulation()
  if _G.vehicle_handle == nil then
    return
  end
  local ok = oxygen.physics.vehicle.set_control_input(
    _G.vehicle_handle, { forward = 0.25, steering = 0.0, brake = 0.0, hand_brake = 0.0 })
  if ok ~= false then
    error("vehicle.set_control_input must be rejected in fixed_simulation")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "vehicle_bindings_phase_gate_contracts" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  RunSceneMutationPhase(
    module, *physics_module_, observer_ptr<engine::FrameContext> { &context_ });
  RunGameplayPhase(
    module, *physics_module_, observer_ptr<engine::FrameContext> { &context_ });
  if (context_.HasErrors()) {
    const auto errors = context_.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }

  ASSERT_EQ(fake_system_->State().vehicle_set_control_calls, 1U);
  ASSERT_EQ(fake_system_->State().vehicles.size(), 1U);
  const auto& created = fake_system_->State().vehicles.begin()->second;
  EXPECT_FLOAT_EQ(created.control_input.forward, 0.65F);
  EXPECT_FLOAT_EQ(created.control_input.brake, 0.0F);
  EXPECT_FLOAT_EQ(created.control_input.steering, -0.15F);
  EXPECT_FLOAT_EQ(created.control_input.hand_brake, 0.0F);

  RunFixedSimulationPhase(
    module, *physics_module_, observer_ptr<engine::FrameContext> { &context_ });
  if (context_.HasErrors()) {
    const auto errors = context_.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }

  // Fixed-simulation writes must be rejected; gameplay-authored value remains.
  EXPECT_EQ(fake_system_->State().vehicle_set_control_calls, 1U);
  const auto& after_fixed = fake_system_->State().vehicles.begin()->second;
  EXPECT_FLOAT_EQ(after_fixed.control_input.forward, 0.65F);
}

NOLINT_TEST_F(PhysicsVehicleBindingsIntegrationTest,
  VehicleLookupApisRespectExactAndAncestorSemantics)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  SetupTestContext();

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  _G.chassis_node = oxygen.scene.create_node("LookupVehicleChassis", nil)
  _G.child_node = oxygen.scene.create_node("LookupVehicleChild", _G.chassis_node)
end
)lua" },
    .chunk_name = ScriptChunkName { "vehicle_lookup_scene_setup" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  RunSceneMutationPhase(
    module, *physics_module_, observer_ptr<engine::FrameContext> { &context_ });
  if (context_.HasErrors()) {
    const auto errors = context_.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }

  auto chassis_node_handle = scene::NodeHandle {};
  for (const auto& root : scene_->GetRootNodes()) {
    if (root.GetName() == "LookupVehicleChassis") {
      chassis_node_handle = root.GetHandle();
      break;
    }
  }
  ASSERT_NE(chassis_node_handle, scene::NodeHandle {});

  physics::body::BodyDesc body_desc {};
  body_desc.type = physics::body::BodyType::kDynamic;
  const auto chassis = fake_system_->Bodies().CreateBody(
    physics_module_->GetWorldId(), body_desc);
  const auto wheel_left = fake_system_->Bodies().CreateBody(
    physics_module_->GetWorldId(), body_desc);
  const auto wheel_right = fake_system_->Bodies().CreateBody(
    physics_module_->GetWorldId(), body_desc);
  ASSERT_TRUE(chassis.has_value());
  ASSERT_TRUE(wheel_left.has_value());
  ASSERT_TRUE(wheel_right.has_value());

  const std::array<physics::vehicle::VehicleWheelDesc, 2> wheel_descs {
    physics::vehicle::VehicleWheelDesc {
      .body_id = wheel_left.value(),
      .axle_index = 0U,
      .side = physics::vehicle::VehicleWheelSide::kLeft,
    },
    physics::vehicle::VehicleWheelDesc {
      .body_id = wheel_right.value(),
      .axle_index = 0U,
      .side = physics::vehicle::VehicleWheelSide::kRight,
    },
  };
  const std::array<uint8_t, 1> vehicle_blob { 0x1U };
  const auto vehicle
    = fake_system_->Vehicles().CreateVehicle(physics_module_->GetWorldId(),
      physics::vehicle::VehicleDesc {
        .chassis_body_id = chassis.value(),
        .wheels = wheel_descs,
        .constraint_settings_blob = vehicle_blob,
      });
  ASSERT_TRUE(vehicle.has_value());

  physics_module_->RegisterNodeAggregateMapping(chassis_node_handle,
    vehicle.value(), physics::aggregate::AggregateAuthority::kCommand);

  const auto lookup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local vehicle = oxygen.physics.vehicle
if vehicle.get_exact(_G.chassis_node) == nil then
  error("vehicle.get_exact must resolve mapped chassis node")
end
if vehicle.get_exact(_G.child_node) ~= nil then
  error("vehicle.get_exact must not traverse ancestors")
end
if vehicle.find_in_ancestors(_G.child_node) == nil then
  error("vehicle.find_in_ancestors must traverse to mapped chassis node")
end
)lua" },
    .chunk_name = ScriptChunkName { "vehicle_lookup_contract" },
  });
  EXPECT_TRUE(lookup_result.ok) << lookup_result.message;
  EXPECT_EQ(lookup_result.stage, "ok");
}

} // namespace oxygen::scripting::test
