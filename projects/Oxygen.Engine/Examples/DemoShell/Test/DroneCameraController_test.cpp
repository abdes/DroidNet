//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include "DemoShell/UI/DroneCameraController.h"
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::testing {
namespace {
  constexpr auto kStep
    = time::CanonicalDuration { std::chrono::milliseconds(16) };
  constexpr float kTolerance = 0.0001F;
  constexpr float kRouteRadius = 10.0F;
  constexpr float kAltitude = 6.0F;

  auto Route() -> std::vector<glm::vec3>
  {
    return {
      { kRouteRadius, 0.0F, kAltitude },
      { 0.0F, kRouteRadius, kAltitude },
      { -kRouteRadius, 0.0F, kAltitude },
      { 0.0F, -kRouteRadius, kAltitude },
    };
  }
} // namespace

NOLINT_TEST(DroneCameraController, StopFreezesPoseAndProgress)
{
  auto scene = std::make_shared<scene::Scene>("Drone", 8U);
  auto camera = scene->CreateNode("Camera");
  ui::DroneCameraController drone;
  drone.SetPathGenerator(Route);
  drone.Start();
  drone.Update(camera, kStep);
  drone.Stop();
  const auto position = camera.GetTransform().GetLocalPosition();
  const auto rotation = camera.GetTransform().GetLocalRotation();
  const auto progress = drone.GetProgress();
  drone.Update(camera, time::CanonicalDuration { std::chrono::seconds(1) });
  EXPECT_EQ(camera.GetTransform().GetLocalPosition(), position);
  EXPECT_EQ(camera.GetTransform().GetLocalRotation(), rotation);
  EXPECT_EQ(drone.GetProgress(), progress);
}

NOLINT_TEST(DroneCameraController, RejoinStartsNearCurrentCamera)
{
  auto scene = std::make_shared<scene::Scene>("Drone", 8U);
  auto camera = scene->CreateNode("Camera");
  const glm::vec3 start { -kRouteRadius, 0.0F, kAltitude };
  camera.GetTransform().SetLocalPosition(start);
  ui::DroneCameraController drone;
  drone.SetPathGenerator(Route);
  drone.SyncFromTransform(camera);
  constexpr double kOppositeSideProgress = 0.5;
  EXPECT_NEAR(drone.GetProgress(), kOppositeSideProgress, kTolerance);
  drone.Start();
  drone.Update(camera, kStep);
  const auto actual = camera.GetTransform().GetLocalPosition();
  if (!actual) {
    FAIL() << "Camera position is unavailable";
    return;
  }
  EXPECT_LT(glm::distance(*actual, start), 1.0F);
}

NOLINT_TEST(DroneCameraController, VerticalRouteAndZeroDeltaStayFinite)
{
  auto scene = std::make_shared<scene::Scene>("Drone", 8U);
  auto camera = scene->CreateNode("Camera");
  ui::DroneCameraController drone;
  drone.SetPathGenerator([] -> std::vector<glm::vec3> {
    return std::vector<glm::vec3> {
      { 0.0F, 0.0F, kAltitude },
      { 0.0F, 0.0F, kAltitude + kRouteRadius },
    };
  });
  drone.SetFocusTarget({ 0.0F, 0.0F, 0.0F });
  drone.SetFocusStrength(1.0F);
  drone.Start();
  drone.Update(camera, kStep);
  const auto position = camera.GetTransform().GetLocalPosition();
  const auto rotation = camera.GetTransform().GetLocalRotation();
  if (!position || !rotation) {
    FAIL() << "Camera transform is unavailable";
    return;
  }
  EXPECT_TRUE(std::isfinite(position.value().x)
    && std::isfinite(position.value().y) && std::isfinite(position.value().z));
  EXPECT_TRUE(std::isfinite(rotation.value().x)
    && std::isfinite(rotation.value().y) && std::isfinite(rotation.value().z)
    && std::isfinite(rotation.value().w));
  drone.Update(camera, time::CanonicalDuration {});
  EXPECT_EQ(camera.GetTransform().GetLocalPosition(), position);
  EXPECT_EQ(camera.GetTransform().GetLocalRotation(), rotation);
}

NOLINT_TEST(DroneCameraController, ClearingOrCollapsingRouteDisablesFlight)
{
  ui::DroneCameraController drone;
  drone.SetPathGenerator(Route);
  drone.SetPathGenerator({});
  EXPECT_FALSE(drone.HasPath());
  drone.SetPathGenerator([] -> std::vector<glm::vec3> {
    return std::vector<glm::vec3>(4U, glm::vec3(0.0F));
  });
  EXPECT_FALSE(drone.HasPath());
  EXPECT_EQ(drone.GetPathLength(), 0.0);
}
} // namespace oxygen::examples::testing
