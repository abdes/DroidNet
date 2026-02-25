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
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakePhysicsSystem.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::scripting::test {

using oxygen::co::testing::TestEventLoop;

namespace {
  auto RunGameplayPhase(ScriptingModule& module,
    oxygen::physics::PhysicsModule& phys_module,
    observer_ptr<engine::FrameContext> context) -> void
  {
    context->SetCurrentPhase(oxygen::core::PhaseId::kGameplay,
      engine::internal::EngineTagFactory::Get());
    TestEventLoop loop;
    oxygen::co::Run(loop, [&module, &phys_module, context]() -> co::Co<> {
      co_await phys_module.OnGameplay(context);
      co_await module.OnGameplay(context);
    });
  }

  auto RunSceneMutationPhase(ScriptingModule& module,
    oxygen::physics::PhysicsModule& phys_module,
    observer_ptr<engine::FrameContext> context) -> void
  {
    context->SetCurrentPhase(oxygen::core::PhaseId::kSceneMutation,
      engine::internal::EngineTagFactory::Get());
    TestEventLoop loop;
    oxygen::co::Run(loop, [&module, &phys_module, context]() -> co::Co<> {
      co_await phys_module.OnSceneMutation(context);
      co_await module.OnSceneMutation(context);
    });
  }
} // namespace

class PhysicsBodyBindingsTest : public ScriptingModuleTest {
protected:
  void SetUp() override
  {
    ScriptingModuleTest::SetUp();

    auto fake_system
      = std::make_unique<oxygen::physics::test::detail::FakePhysicsSystem>();
    fake_system_ = fake_system.get();
    fake_system_->State().world_created = true;
    fake_system_->State().world_id = oxygen::physics::WorldId { 1 };

    physics_module_ = std::make_unique<oxygen::physics::PhysicsModule>(
      oxygen::engine::kPhysicsModulePriority, std::move(fake_system));

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
    scene_
      = std::make_shared<scene::Scene>("physics_test", kDefaultSceneCapacity);
    context_.SetFrameSequenceNumber(frame::SequenceNumber { 1 }, Tag::Get());
    context_.SetScene(observer_ptr<scene::Scene> { scene_.get() });
  }

protected:
  std::shared_ptr<scene::Scene> scene_;
  engine::FrameContext context_;
  oxygen::physics::test::detail::FakePhysicsSystem* fake_system_ { nullptr };
  std::unique_ptr<oxygen::physics::PhysicsModule> physics_module_;
};

NOLINT_TEST_F(
  PhysicsBodyBindingsTest, PhysicsBodyBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local body = oxygen.physics.body
if type(body.attach) ~= "function" then error("missing body.attach") end
if type(body.get) ~= "function" then error("missing body.get") end

if body.get(nil) ~= nil then
  error("body.get should return nil for non-node input")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_body_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsBodyBindingsTest, PhysicsBodyBindingsRejectInvalidNodeAndHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local body = oxygen.physics.body

if body.attach(nil, {}) ~= nil then
  error("body.attach must return nil for invalid scene node shape")
end

if body.get(nil) ~= nil then
  error("body.get must return nil for invalid scene node shape")
end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_body_invalid_node_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsBodyBindingsTest, PhysicsBodyAttachRejectsInvalidProperties)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  SetupTestContext();

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  _G.test_node = oxygen.scene.create_node("PhysNode", nil)
end

function on_gameplay()
  local body = oxygen.physics.body

  local ok, err = pcall(function()
    body.attach(_G.test_node, { body_type = "invalid_type" })
  end)
  if ok then error("body.attach should reject invalid body_type string") end

  local ok2, err2 = pcall(function()
    body.attach(_G.test_node, { mass_kg = "not_a_number" })
  end)
  if ok2 then error("body.attach should reject string for mass_kg") end

  local ok3, err3 = pcall(function()
    body.attach(_G.test_node, { linear_damping = {} })
  end)
  if ok3 then error("body.attach should reject table for linear_damping") end

  local ok4, err4 = pcall(function()
    body.attach(_G.test_node, { shape_id = "invalid_userdata" })
  end)
  if ok4 then error("body.attach should reject string for shape_id") end
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_body_attach_properties" },
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
}

NOLINT_TEST_F(
  PhysicsBodyBindingsTest, PhysicsBodyHandleMethodsRejectInvalidArguments)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  SetupTestContext();

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  _G.test_node = oxygen.scene.create_node("KineNode", nil)
end

function on_gameplay()
  local handle = oxygen.physics.body.attach(_G.test_node, {
    body_type = "kinematic"
  })
  if handle == nil then error("body.attach failed") end

  -- move_kinematic argument boundaries
  local ok, err = pcall(function()
    handle:move_kinematic(nil, oxygen.math.quat(1,0,0,0), 0.016)
  end)
  if ok then error("move_kinematic should reject nil position") end

  local ok2, err2 = pcall(function()
    handle:move_kinematic(oxygen.math.vec3(1,2,3), nil, 0.016)
  end)
  if ok2 then error("move_kinematic should reject nil rotation") end

  local ok3, err3 = pcall(function()
    handle:move_kinematic(oxygen.math.vec3(1,2,3), oxygen.math.quat(1,0,0,0))
  end)
  if ok3 then error("move_kinematic should reject missing delta_time") end

  -- set_linear_velocity argument boundaries
  local ok4, err4 = pcall(function()
    handle:set_linear_velocity()
  end)
  if ok4 then error("set_linear_velocity should reject missing vector") end

  local ok5, err5 = pcall(function()
    handle:set_linear_velocity("scalar_not_vector")
  end)
  if ok5 then error("set_linear_velocity should reject string") end
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_body_method_boundaries" },
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
}

} // namespace oxygen::scripting::test
