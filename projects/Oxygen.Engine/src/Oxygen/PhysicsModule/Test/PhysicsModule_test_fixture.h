//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakePhysicsSystem.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics::test {

class PhysicsModuleSyncTest : public testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<scene::Scene>("PhysicsModuleSyncTest", 128);
    frame_.SetScene(observer_ptr<scene::Scene> { scene_.get() });

    engine::ModuleTimingData timing {};
    timing.fixed_delta_time
      = time::CanonicalDuration { std::chrono::milliseconds(16) };
    frame_.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());

    auto fake_physics = std::make_unique<detail::FakePhysicsSystem>();
    fake_physics_
      = observer_ptr<detail::FakePhysicsSystem> { fake_physics.get() };
    module_ = std::make_unique<PhysicsModule>(
      engine::ModulePriority { 100U }, std::move(fake_physics));
  }

  void TearDown() override
  {
    if (module_) {
      module_->OnShutdown();
    }
  }

  auto RunPhase(core::PhaseId phase,
    co::Co<> (PhysicsModule::*phase_fn)(observer_ptr<engine::FrameContext>))
    -> void
  {
    frame_.SetCurrentPhase(phase, engine::internal::EngineTagFactory::Get());
    co::Run(loop_, [&]() -> co::Co<> {
      co_await (module_.get()->*phase_fn)(
        observer_ptr<engine::FrameContext> { &frame_ });
      co_return;
    });
  }

  auto RunGameplay() -> void
  {
    RunPhase(core::PhaseId::kGameplay, &PhysicsModule::OnGameplay);
  }

  auto RunSceneMutation() -> void
  {
    RunPhase(core::PhaseId::kSceneMutation, &PhysicsModule::OnSceneMutation);
  }

  auto RunFixedSimulation() -> void
  {
    RunPhase(
      core::PhaseId::kFixedSimulation, &PhysicsModule::OnFixedSimulation);
  }

  [[nodiscard]] auto AttachBody(scene::SceneNode& node,
    const body::BodyType type) -> std::optional<RigidBodyFacade>
  {
    RunGameplay();
    scene_->Update();

    body::BodyDesc desc {};
    desc.type = type;
    return ScenePhysics::AttachRigidBody(
      observer_ptr<PhysicsModule> { module_.get() }, node, desc);
  }

  auto SetActiveScene(std::shared_ptr<scene::Scene> scene) -> void
  {
    scene_ = std::move(scene);
    frame_.SetScene(observer_ptr<scene::Scene> { scene_.get() });
  }

  [[nodiscard]] auto FakeState() noexcept -> detail::BackendState&
  {
    return fake_physics_->State();
  }

  std::shared_ptr<scene::Scene> scene_;
  engine::FrameContext frame_;
  co::testing::TestEventLoop loop_ {};
  std::unique_ptr<PhysicsModule> module_;
  observer_ptr<detail::FakePhysicsSystem> fake_physics_ {};
};

} // namespace oxygen::physics::test
