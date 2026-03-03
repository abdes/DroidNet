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

class PhysicsSoftBodyBindingsTest : public ScriptingModuleTest { };

using oxygen::co::testing::TestEventLoop;

namespace {
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
} // namespace

class PhysicsSoftBodyBindingsIntegrationTest : public ScriptingModuleTest {
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
      "soft_body_bindings_test", kDefaultSceneCapacity);
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
  PhysicsSoftBodyBindingsTest, PhysicsSoftBodyBindingsNoEngineFallbackContracts)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local soft = oxygen.physics.soft_body
if type(soft.create) ~= "function" then error("missing soft_body.create") end
if type(soft.get_exact) ~= "function" then error("missing soft_body.get_exact") end
if type(soft.find_in_ancestors) ~= "function" then error("missing soft_body.find_in_ancestors") end
if type(soft.destroy) ~= "function" then error("missing soft_body.destroy") end
if type(soft.set_material_params) ~= "function" then error("missing soft_body.set_material_params") end
if type(soft.get_state) ~= "function" then error("missing soft_body.get_state") end
if type(soft.get_authority) ~= "function" then error("missing soft_body.get_authority") end
if type(soft.flush_structural_changes) ~= "function" then error("missing soft_body.flush_structural_changes") end

if soft.create({ cluster_count = 1 }) ~= nil then
  error("soft_body.create should return nil without module")
end
if soft.flush_structural_changes() ~= nil then
  error("soft_body.flush_structural_changes should return nil without module")
end
)lua" },
    .chunk_name = ScriptChunkName { "physics_soft_body_no_engine_fallbacks" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  PhysicsSoftBodyBindingsTest, PhysicsSoftBodyBindingsRejectInvalidHandleShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local soft = oxygen.physics.soft_body

local ok = pcall(function() soft.destroy(nil) end)
if ok then error("soft_body.destroy must reject invalid handle") end

if soft.get_exact(nil) ~= nil then
  error("soft_body.get_exact should return nil for invalid scene node")
end
if soft.find_in_ancestors(nil) ~= nil then
  error("soft_body.find_in_ancestors should return nil for invalid scene node")
end

ok = pcall(function() soft.set_material_params(nil, {}) end)
if ok then error("soft_body.set_material_params must reject invalid handle") end

ok = pcall(function() soft.get_state(nil) end)
if ok then error("soft_body.get_state must reject invalid handle") end

ok = pcall(function() soft.get_authority(nil) end)
if ok then error("soft_body.get_authority must reject invalid handle") end
)lua" },
    .chunk_name
    = ScriptChunkName { "physics_soft_body_invalid_handle_contracts" },
  });
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(PhysicsSoftBodyBindingsIntegrationTest,
  SoftBodyLookupApisRespectExactAndAncestorSemantics)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  SetupTestContext();

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_scene_mutation()
  _G.soft_body_node = oxygen.scene.create_node("LookupSoftBodyRoot", nil)
  _G.soft_body_child = oxygen.scene.create_node("LookupSoftBodyChild", _G.soft_body_node)
end
)lua" },
    .chunk_name = ScriptChunkName { "soft_body_lookup_scene_setup" },
  });
  ASSERT_TRUE(hook_result.ok) << hook_result.message;

  RunSceneMutationPhase(
    module, *physics_module_, observer_ptr<engine::FrameContext> { &context_ });
  if (context_.HasErrors()) {
    const auto errors = context_.GetErrors();
    FAIL() << (errors.empty() ? std::string("unknown scripting error")
                              : errors.front().message);
  }

  auto soft_body_node_handle = scene::NodeHandle {};
  for (const auto& root : scene_->GetRootNodes()) {
    if (root.GetName() == "LookupSoftBodyRoot") {
      soft_body_node_handle = root.GetHandle();
      break;
    }
  }
  ASSERT_NE(soft_body_node_handle, scene::NodeHandle {});

  const std::array<uint8_t, 1> soft_blob { 0x1U };
  const auto soft_body
    = fake_system_->SoftBodies().CreateSoftBody(physics_module_->GetWorldId(),
      physics::softbody::SoftBodyDesc {
        .cluster_count = 4U,
        .settings_blob = soft_blob,
      });
  ASSERT_TRUE(soft_body.has_value());

  physics_module_->RegisterNodeAggregateMapping(soft_body_node_handle,
    soft_body.value(), physics::aggregate::AggregateAuthority::kSimulation);

  const auto lookup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local soft = oxygen.physics.soft_body
if soft.get_exact(_G.soft_body_node) == nil then
  error("soft_body.get_exact must resolve mapped node")
end
if soft.get_exact(_G.soft_body_child) ~= nil then
  error("soft_body.get_exact must not traverse ancestors")
end
if soft.find_in_ancestors(_G.soft_body_child) == nil then
  error("soft_body.find_in_ancestors must traverse to mapped ancestor")
end
)lua" },
    .chunk_name = ScriptChunkName { "soft_body_lookup_contract" },
  });
  EXPECT_TRUE(lookup_result.ok) << lookup_result.message;
  EXPECT_EQ(lookup_result.stage, "ok");
}

} // namespace oxygen::scripting::test
