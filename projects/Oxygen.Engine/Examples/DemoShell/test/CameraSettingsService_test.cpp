//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::testing {
namespace {

  constexpr std::size_t kSceneCapacity = 8;
  constexpr ViewPort kViewport { .width = 1280.0F, .height = 720.0F };

  //! Camera settings tests share the process-wide demo settings, held in
  //! memory only and emptied before each test.
  class CameraSettingsServiceTest : public ::testing::Test {
  protected:
    static auto SetUpTestSuite() -> void
    {
      SettingsService::InitializeForDemoApp(
        std::filesystem::temp_directory_path()
        / ("oxygen-camera-settings-" + Uuid::Generate().ToString() + ".json"));
      SettingsService::ForDemoApp()->SetPersistenceEnabled(false);
    }

    auto SetUp() -> void override { Settings().Load(); }

    static auto Settings() -> SettingsService&
    {
      return *SettingsService::ForDemoApp();
    }

    struct World {
      std::shared_ptr<scene::Scene> scene;
      scene::SceneNode camera;
    };

    //! The names of a scene and of its camera.
    struct WorldNames {
      std::string scene;
      std::string camera;
    };

    //! A scene with a perspective camera at the origin.
    static auto MakeWorld(const WorldNames& names) -> World
    {
      auto scene = std::make_shared<scene::Scene>(names.scene, kSceneCapacity);
      auto node = scene->CreateNode(names.camera);
      EXPECT_TRUE(
        node.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
      return { .scene = std::move(scene), .camera = node };
    }

    //! Activate `world` and make its camera the main view's.
    static auto Activate(CameraSettingsService& service, World& world) -> void
    {
      service.OnSceneActivated(*world.scene);
      service.OnMainViewReady(ViewId { 1U }, world.camera, kViewport);
    }

    //! Activate `world`, move its camera to `position` and save it; a
    //! position equal to the current one is not a change and saves nothing.
    static auto SavePose(CameraSettingsService& service, World& world,
      const Vec3& position) -> void
    {
      Activate(service, world);
      EXPECT_TRUE(world.camera.GetTransform().SetLocalPosition(position));
      service.PersistActiveCameraSettings();
    }
  };

  //! Names become single key segments; spellings that differ only in
  //! separators share one entry.
  NOLINT_TEST_F(CameraSettingsServiceTest, KeysAreSingleSegments)
  {
    EXPECT_EQ(CameraSettingsService::CameraKey("Level.01", "New Entity 2"),
      "camera_rig.scenes.Level_01.cameras.New_Entity_2");
    EXPECT_EQ(CameraSettingsService::CameraKey("Level.01", "New Entity 2"),
      CameraSettingsService::CameraKey("Level_01", "New_Entity_2"));
    EXPECT_EQ(CameraSettingsService::CameraKey("", "Camera"),
      "camera_rig.scenes._.cameras.Camera");
  }

  //! Two scenes whose cameras share a name keep separate poses, and going
  //! back to a scene restores its own.
  NOLINT_TEST_F(CameraSettingsServiceTest, EachSceneRestoresItsOwnCameraPose)
  {
    CameraSettingsService service;
    auto first = MakeWorld({ .scene = "First", .camera = "MainCamera" });
    auto second = MakeWorld({ .scene = "Second", .camera = "MainCamera" });
    SavePose(service, first, { 1.0F, 2.0F, 3.0F });
    SavePose(service, second, { -4.0F, 5.0F, -6.0F });

    auto first_again = MakeWorld({ .scene = "First", .camera = "MainCamera" });
    Activate(service, first_again);

    EXPECT_EQ(first_again.camera.GetTransform().GetLocalPosition(),
      Vec3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(Settings().GetFloat(
                CameraSettingsService::CameraKey("Second", "MainCamera")
                + ".position.x"),
      -4.0F);
  }

  //! Only the most recently activated scenes keep their camera settings; a
  //! scene activated again counts as recent.
  NOLINT_TEST_F(CameraSettingsServiceTest, ForgetsTheLeastRecentlyUsedScenes)
  {
    CameraSettingsService service;
    const auto scene_name = [](const std::size_t i) -> std::string {
      return "Scene" + std::to_string(i);
    };
    for (std::size_t i = 0; i < CameraSettingsService::kRememberedScenes; ++i) {
      auto world = MakeWorld({ .scene = scene_name(i), .camera = "Camera" });
      SavePose(service, world, { static_cast<float>(i + 1U), 0.0F, 0.0F });
    }
    // Scene0 is the oldest; using it again makes Scene1 the oldest.
    auto oldest = MakeWorld({ .scene = scene_name(0), .camera = "Camera" });
    Activate(service, oldest);

    auto newest = MakeWorld({ .scene = "Newest", .camera = "Camera" });
    SavePose(service, newest, { 9.0F, 0.0F, 0.0F });

    const auto saved = [](const std::string& scene) -> bool {
      return Settings().Contains(
        CameraSettingsService::CameraKey(scene, "Camera"));
    };
    EXPECT_TRUE(saved(scene_name(0)));
    EXPECT_FALSE(saved(scene_name(1)));
    EXPECT_TRUE(saved("Newest"));
    EXPECT_EQ(Settings().GetChildKeys("camera_rig.scenes").size(),
      CameraSettingsService::kRememberedScenes);
  }

  //! Cameras saved by name alone, from before settings were kept per scene,
  //! are removed; the shared orbit mode is not camera state and stays.
  NOLINT_TEST_F(CameraSettingsServiceTest, RemovesCamerasSavedWithoutAScene)
  {
    Settings().SetFloat("camera_rig.MainCamera.position.x", 7.0F);
    Settings().SetString("camera_rig.MainCamera.orbit.mode", "turntable");
    Settings().SetString("camera.orbit_mode", "trackball");
    CameraSettingsService service;
    auto world = MakeWorld({ .scene = "Level", .camera = "MainCamera" });

    Activate(service, world);

    EXPECT_FALSE(Settings().Contains("camera_rig.MainCamera"));
    EXPECT_EQ(world.camera.GetTransform().GetLocalPosition(), Vec3(0.0F));
    EXPECT_EQ(Settings().GetString("camera.orbit_mode"), "trackball");
  }

} // namespace
} // namespace oxygen::examples::testing
