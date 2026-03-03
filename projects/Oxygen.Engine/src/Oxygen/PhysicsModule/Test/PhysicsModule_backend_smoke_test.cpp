//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <chrono>

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Physics/Physics.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::physics::test {

NOLINT_TEST(PhysicsModuleBackendSmokeTest, InjectedBackendRunsBasicLifecycle)
{
  using Tag = oxygen::engine::internal::EngineTagFactory;

  auto physics_system_result = CreatePhysicsSystem(GetSelectedBackend());
  if (!physics_system_result.has_value()) {
    ASSERT_EQ(physics_system_result.error(), PhysicsError::kBackendUnavailable);
    SUCCEED();
    return;
  }

  engine::FrameContext frame {};
  auto scene = std::make_shared<scene::Scene>("PhysicsBackendSmoke", 64);
  frame.SetScene(observer_ptr<scene::Scene> { scene.get() });

  engine::ModuleTimingData timing {};
  timing.fixed_delta_time
    = time::CanonicalDuration { std::chrono::milliseconds(16) };
  frame.SetModuleTimingData(timing, Tag::Get());

  auto module = std::make_unique<PhysicsModule>(
    engine::ModulePriority { 100U }, std::move(physics_system_result.value()));
  co::testing::TestEventLoop loop {};

  frame.SetCurrentPhase(core::PhaseId::kGameplay, Tag::Get());
  co::Run(loop, [&]() -> co::Co<> {
    co_await module->OnGameplay(observer_ptr<engine::FrameContext> { &frame });
    co_return;
  });

  auto node = scene->CreateNode("smoke-node");
  ASSERT_TRUE(node.IsValid());
  scene->Update();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kKinematic;
  ASSERT_TRUE(ScenePhysics::AttachRigidBody(
    observer_ptr<PhysicsModule> { module.get() }, node, desc)
      .has_value());

  frame.SetCurrentPhase(core::PhaseId::kFixedSimulation, Tag::Get());
  co::Run(loop, [&]() -> co::Co<> {
    co_await module->OnFixedSimulation(
      observer_ptr<engine::FrameContext> { &frame });
    co_return;
  });

  frame.SetCurrentPhase(core::PhaseId::kSceneMutation, Tag::Get());
  co::Run(loop, [&]() -> co::Co<> {
    co_await module->OnSceneMutation(
      observer_ptr<engine::FrameContext> { &frame });
    co_return;
  });

  module->OnShutdown();
}

} // namespace oxygen::physics::test
