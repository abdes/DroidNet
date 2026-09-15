//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::examples::testing {
namespace {

  constexpr float kTolerance = 0.0001F;
  constexpr size_t kSceneCapacity = 8;
  constexpr float kOrbitRadius = 8.0F;
  constexpr float kZoomInput = 2.0F;
  constexpr glm::vec2 kOrbitInput { 25.0F, -12.0F };
  constexpr glm::vec3 kWorldPivot { 21.0F, -8.0F, 6.0F };
  constexpr glm::quat kAuthoredTilt { 0.9537F, 0.3007F, 0.0F, 0.0F };
  constexpr float kAuthoredRollRadians = 0.35F;
  constexpr ViewPort kViewport { .width = 1280.0F, .height = 720.0F };
  const auto kFrameTime
    = time::CanonicalDuration(std::chrono::milliseconds(16));

  template <typename T> auto CheckedValue(std::optional<T> value) -> T
  {
    if (!value.has_value()) {
      ADD_FAILURE() << "Expected a present optional test value";
      throw std::bad_optional_access {};
    }
    return std::move(*value);
  }

  auto ExpectPosition(const glm::vec3& actual, const glm::vec3& expected)
    -> void
  {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
  }

  auto ExpectRotation(const glm::quat& actual, const glm::quat& expected)
    -> void
  {
    EXPECT_NEAR(std::abs(glm::dot(actual, expected)), 1.0F, kTolerance);
  }

  auto AuthoredRotation() -> glm::quat
  {
    // Camera-local -Z points into the Z-up world; include roll to ensure idle
    // synchronization does not silently constrain an authored orientation.
    return glm::normalize(kAuthoredTilt
      * glm::angleAxis(kAuthoredRollRadians, space::look::Forward));
  }

} // namespace

NOLINT_TEST(OrbitCameraController, SyncThenIdlePreservesAuthoredPose)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto scene = std::make_shared<scene::Scene>("OrbitSync", kSceneCapacity);
    auto camera = scene->CreateNode("Camera");
    const glm::vec3 position(10.0F, -16.0F, 9.5F);
    const auto rotation = AuthoredRotation();
    ASSERT_TRUE(camera.GetTransform().SetLocalPosition(position));
    ASSERT_TRUE(camera.GetTransform().SetLocalRotation(rotation));

    ui::OrbitCameraController controller;
    controller.SetMode(mode);
    controller.SyncFromTransform(camera);
    const auto radius = glm::length(position);
    EXPECT_NEAR(controller.GetDistance(), radius, kTolerance);
    ExpectPosition(controller.GetTarget(),
      position + (rotation * space::look::Forward) * radius);
    for (int frame = 0; frame < 3; ++frame) {
      controller.Update(camera, kFrameTime);
      ExpectPosition(
        CheckedValue(camera.GetTransform().GetLocalPosition()), position);
      ExpectRotation(
        CheckedValue(camera.GetTransform().GetLocalRotation()), rotation);
    }
  }
}

NOLINT_TEST(OrbitCameraController, ExplicitPivotAndZoomRemainAuthoritative)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto scene = std::make_shared<scene::Scene>("OrbitPivot", kSceneCapacity);
    auto camera = scene->CreateNode("Camera");
    const auto rotation = AuthoredRotation();
    ASSERT_TRUE(camera.GetTransform().SetLocalPosition({ 10, -16, 9.5F }));
    ASSERT_TRUE(camera.GetTransform().SetLocalRotation(rotation));

    ui::OrbitCameraController controller;
    controller.SetMode(mode);
    controller.SyncFromTransform(camera);
    const glm::vec3 pivot(4.0F, 2.0F, 3.0F);
    controller.SetTarget(pivot);
    controller.SetDistance(kOrbitRadius);
    controller.Update(camera, kFrameTime);
    const auto forward = rotation * space::look::Forward;
    ExpectPosition(CheckedValue(camera.GetTransform().GetLocalPosition()),
      pivot - forward * kOrbitRadius);
    ExpectRotation(
      CheckedValue(camera.GetTransform().GetLocalRotation()), rotation);

    controller.AddZoomInput(kZoomInput);
    controller.Update(camera, kFrameTime);
    const auto radius = kOrbitRadius - (kZoomInput * controller.GetZoomStep());
    EXPECT_FLOAT_EQ(controller.GetDistance(), radius);
    ExpectPosition(controller.GetTarget(), pivot);
    ExpectPosition(CheckedValue(camera.GetTransform().GetLocalPosition()),
      pivot - forward * radius);
    ExpectRotation(
      CheckedValue(camera.GetTransform().GetLocalRotation()), rotation);
  }
}

NOLINT_TEST(OrbitCameraController, OrbitInputRotatesAroundSynchronizedPivot)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto scene = std::make_shared<scene::Scene>("OrbitInput", kSceneCapacity);
    auto camera = scene->CreateNode("Camera");
    ASSERT_TRUE(camera.GetTransform().SetLocalPosition({ 10, -16, 9.5F }));
    const auto before_rotation = AuthoredRotation();
    ASSERT_TRUE(camera.GetTransform().SetLocalRotation(before_rotation));

    ui::OrbitCameraController controller;
    controller.SetMode(mode);
    controller.SyncFromTransform(camera);
    const auto pivot = controller.GetTarget();
    const auto radius = controller.GetDistance();
    controller.AddOrbitInput(kOrbitInput);
    controller.Update(camera, kFrameTime);
    const auto position
      = CheckedValue(camera.GetTransform().GetLocalPosition());
    const auto rotation
      = CheckedValue(camera.GetTransform().GetLocalRotation());
    ExpectPosition(controller.GetTarget(), pivot);
    EXPECT_NEAR(glm::distance(position, pivot), radius, kTolerance);
    ExpectPosition(
      position + (rotation * space::look::Forward) * radius, pivot);
    EXPECT_LT(std::abs(glm::dot(rotation, before_rotation)), 0.999F);
  }
}

NOLINT_TEST(OrbitCameraController, ParentLocalAndWorldPoseSurviveIdleSync)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto scene
      = std::make_shared<scene::Scene>("ParentedOrbit", kSceneCapacity);
    auto parent = scene->CreateNode("Parent");
    ASSERT_TRUE(parent.GetTransform().SetLocalPosition({ -7, 11, 4 }));
    ASSERT_TRUE(parent.GetTransform().SetLocalRotation(
      glm::angleAxis(0.6F, space::move::Up)));
    ASSERT_TRUE(parent.GetTransform().SetLocalScale({ 2, 2, 2 }));
    auto child_result = scene->CreateChildNode(parent, "Camera");
    ASSERT_TRUE(child_result.has_value());
    auto child = CheckedValue(child_result);
    const glm::vec3 position(10.0F, -16.0F, 9.5F);
    const auto rotation = AuthoredRotation();
    ASSERT_TRUE(child.GetTransform().SetLocalPosition(position));
    ASSERT_TRUE(child.GetTransform().SetLocalRotation(rotation));
    scene->Update();
    const auto world_position = child.GetTransform().GetWorldPosition();
    const auto world_rotation = child.GetTransform().GetWorldRotation();
    ASSERT_TRUE(world_position.has_value());
    ASSERT_TRUE(world_rotation.has_value());

    ui::OrbitCameraController controller;
    controller.SetMode(mode);
    controller.SyncFromTransform(child);
    ExpectPosition(controller.GetTarget(),
      CheckedValue(world_position)
        + (CheckedValue(world_rotation) * space::look::Forward)
          * controller.GetDistance());
    controller.Update(child, kFrameTime);
    scene->Update();
    ExpectPosition(
      CheckedValue(child.GetTransform().GetLocalPosition()), position);
    ExpectRotation(
      CheckedValue(child.GetTransform().GetLocalRotation()), rotation);
    ExpectPosition(CheckedValue(child.GetTransform().GetWorldPosition()),
      CheckedValue(world_position));
  }
}

NOLINT_TEST(OrbitCameraController, ParentedCameraUsesWorldPivotForZoomAndOrbit)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    for (const auto parent_scale : { glm::vec3(2.0F),
           glm::vec3(2.0F, 3.0F, 0.75F), glm::vec3(-2.0F, 3.0F, 0.75F) }) {
      for (const auto camera_scale :
        { glm::vec3(1.0F), glm::vec3(-1.0F, 1.5F, 0.5F) }) {
        SCOPED_TRACE(static_cast<int>(mode));
        auto scene
          = std::make_shared<scene::Scene>("WorldOrbit", kSceneCapacity);
        auto parent = scene->CreateNode("Parent");
        ASSERT_TRUE(parent.GetTransform().SetLocalPosition({ -7, 11, 4 }));
        ASSERT_TRUE(parent.GetTransform().SetLocalRotation(
          glm::angleAxis(0.6F, space::move::Up)));
        ASSERT_TRUE(parent.GetTransform().SetLocalScale(parent_scale));
        auto camera_result = scene->CreateChildNode(parent, "Camera");
        ASSERT_TRUE(camera_result.has_value());
        auto camera = CheckedValue(camera_result);
        ASSERT_TRUE(camera.GetTransform().SetLocalTransform(
          { 10, -16, 9.5F }, AuthoredRotation(), camera_scale));

        ui::OrbitCameraController controller;
        controller.SetMode(mode);
        // Deliberately synchronize before Scene::Update: input can see dirty
        // parent transforms and must not read stale checked world caches.
        controller.SyncFromTransform(camera);
        scene->Update();
        const auto initial_rotation
          = CheckedValue(camera.GetTransform().GetWorldRotation());
        const auto world_pivot = kWorldPivot;
        controller.SetTarget(world_pivot);
        controller.SetDistance(kOrbitRadius);
        controller.AddZoomInput(kZoomInput);
        controller.Update(camera, kFrameTime);
        scene->Update();
        const auto radius
          = kOrbitRadius - (kZoomInput * controller.GetZoomStep());
        ExpectPosition(controller.GetTarget(), world_pivot);
        ExpectPosition(CheckedValue(camera.GetTransform().GetWorldPosition()),
          world_pivot - (initial_rotation * space::look::Forward) * radius);
        ExpectRotation(CheckedValue(camera.GetTransform().GetWorldRotation()),
          initial_rotation);
        EXPECT_EQ(
          CheckedValue(camera.GetTransform().GetLocalScale()), camera_scale);

        controller.AddOrbitInput(kOrbitInput);
        controller.Update(camera, kFrameTime);
        scene->Update();
        const auto position
          = CheckedValue(camera.GetTransform().GetWorldPosition());
        const auto rotation
          = CheckedValue(camera.GetTransform().GetWorldRotation());
        ExpectPosition(controller.GetTarget(), world_pivot);
        EXPECT_NEAR(glm::distance(position, world_pivot), radius, kTolerance);
        ExpectPosition(
          position + (rotation * space::look::Forward) * radius, world_pivot);
        EXPECT_EQ(
          CheckedValue(camera.GetTransform().GetLocalScale()), camera_scale);
      }
    }
  }
}

NOLINT_TEST(OrbitCameraController, IgnoreParentTransformKeepsWorldPivotContract)
{
  auto scene
    = std::make_shared<scene::Scene>("IgnoreParentOrbit", kSceneCapacity);
  auto parent = scene->CreateNode("Parent");
  ASSERT_TRUE(parent.GetTransform().SetLocalTransform(
    { -7, 11, 4 }, glm::angleAxis(0.6F, space::move::Up), { 2, 3, 4 }));
  auto camera_result = scene->CreateChildNode(parent, "Camera");
  ASSERT_TRUE(camera_result.has_value());
  auto camera = CheckedValue(camera_result);
  CheckedValue(camera.GetFlags())
    .get()
    .SetLocalValue(scene::SceneNodeFlags::kIgnoreParentTransform, true);
  ASSERT_TRUE(camera.GetTransform().SetLocalRotation(AuthoredRotation()));
  scene->Update();
  ui::OrbitCameraController controller;
  controller.SyncFromTransform(camera);
  const glm::vec3 pivot(4.0F, 2.0F, 3.0F);
  controller.SetTarget(pivot);
  controller.SetDistance(kOrbitRadius);
  controller.Update(camera, kFrameTime);
  scene->Update();
  const auto expected
    = pivot - (AuthoredRotation() * space::look::Forward) * kOrbitRadius;
  ExpectPosition(
    CheckedValue(camera.GetTransform().GetLocalPosition()), expected);
  ExpectPosition(
    CheckedValue(camera.GetTransform().GetWorldPosition()), expected);
  ExpectPosition(controller.GetTarget(), pivot);
}

NOLINT_TEST(OrbitCameraController, SettingsRestoreUsesPersistedWorldPivot)
{
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  // Change only this process's in-memory settings. Reload clears the dirty
  // state even on an assertion failure; no settings file is written/deleted.
  const ScopeGuard restore_settings(
    [&]() noexcept -> void { settings->Load(); });
  constexpr auto prefix = "camera_rig.OrbitWorldRestoreTest";
  settings->SetString(std::string(prefix) + ".mode", "orbit");
  settings->SetString(std::string(prefix) + ".orbit.mode", "turntable");
  settings->SetFloat(std::string(prefix) + ".orbit.target.x", kWorldPivot.x);
  settings->SetFloat(std::string(prefix) + ".orbit.target.y", kWorldPivot.y);
  settings->SetFloat(std::string(prefix) + ".orbit.target.z", kWorldPivot.z);
  settings->SetFloat(std::string(prefix) + ".orbit.distance", kOrbitRadius);

  auto scene
    = std::make_shared<scene::Scene>("RestoreWorldOrbit", kSceneCapacity);
  auto parent = scene->CreateNode("Parent");
  ASSERT_TRUE(parent.GetTransform().SetLocalTransform(
    { -7, 11, 4 }, glm::angleAxis(0.6F, space::move::Up), { 2, 3, 0.75F }));
  auto camera_result = scene->CreateChildNode(parent, "OrbitWorldRestoreTest");
  ASSERT_TRUE(camera_result.has_value());
  auto camera = CheckedValue(camera_result);
  ASSERT_TRUE(
    camera.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  const auto local_rotation = glm::angleAxis(0.6F, space::move::Right);
  ASSERT_TRUE(camera.GetTransform().SetLocalTransform(
    { 10, -16, 9.5F }, local_rotation, { 1, 1, 1 }));
  scene->Update();
  const auto world_rotation
    = CheckedValue(camera.GetTransform().GetWorldRotation());
  const auto pivot = kWorldPivot;

  ui::CameraRigController rig;
  CameraSettingsService service;
  service.BindCameraRig(observer_ptr { &rig });
  service.OnSceneActivated(*scene);
  service.OnRuntimeMainViewReady(camera, kViewport);
  scene->Update();
  ExpectPosition(CheckedValue(camera.GetTransform().GetWorldPosition()),
    pivot - (world_rotation * space::look::Forward) * kOrbitRadius);
  ExpectPosition(rig.GetOrbitController()->GetTarget(), pivot);
  EXPECT_NEAR(
    rig.GetOrbitController()->GetDistance(), kOrbitRadius, kTolerance);
}

NOLINT_TEST(OrbitCameraController, MinimumRadiusDoesNotDisplaceCamera)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    auto scene
      = std::make_shared<scene::Scene>("ZeroRadiusOrbit", kSceneCapacity);
    auto camera = scene->CreateNode("Camera");
    ui::OrbitCameraController controller;
    controller.SetMode(mode);
    controller.SyncFromTransform(camera);
    EXPECT_FLOAT_EQ(controller.GetDistance(), controller.GetMinDistance());
    controller.Update(camera, kFrameTime);
    ExpectPosition(
      CheckedValue(camera.GetTransform().GetLocalPosition()), glm::vec3(0));
  }
}

NOLINT_TEST(OrbitCameraController, RootScaleDoesNotChangeCameraOrientation)
{
  for (const auto mode :
    { ui::OrbitMode::kTurntable, ui::OrbitMode::kTrackball }) {
    auto scene
      = std::make_shared<scene::Scene>("RootScaledOrbit", kSceneCapacity);
    auto camera = scene->CreateNode("Camera");
    const glm::vec3 scale(-1.0F, 1.5F, 0.5F);
    const auto rotation = AuthoredRotation();
    ASSERT_TRUE(camera.GetTransform().SetLocalTransform(
      { 10, -16, 9.5F }, rotation, scale));
    ui::OrbitCameraController controller;
    controller.SetMode(mode);
    controller.SyncFromTransform(camera);
    const glm::vec3 pivot(4.0F, 2.0F, 3.0F);
    controller.SetTarget(pivot);
    controller.SetDistance(kOrbitRadius);
    controller.Update(camera, kFrameTime);
    ExpectPosition(CheckedValue(camera.GetTransform().GetLocalPosition()),
      pivot - (rotation * space::look::Forward) * kOrbitRadius);
    ExpectRotation(
      CheckedValue(camera.GetTransform().GetLocalRotation()), rotation);
    EXPECT_EQ(CheckedValue(camera.GetTransform().GetLocalScale()), scale);
  }
}

} // namespace oxygen::examples::testing
