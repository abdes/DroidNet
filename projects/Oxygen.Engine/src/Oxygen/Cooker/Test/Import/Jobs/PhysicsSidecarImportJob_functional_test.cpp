//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/PhysicsSidecarImportJob.cpp

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/ImportHarness.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::cooker::test::ImportServiceTest;

  using oxygen::cooker::test::HasDiagnosticCode;

  auto MakeSceneDescriptorRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://scene-descriptor";
    request.job_name = "DemoScene";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .normalized_descriptor_json = R"({"version":)"
        + std::to_string(data::pak::world::kSceneAssetVersion)
        + R"(,"name":"DemoScene","nodes":[{"name":"Root"}]})",
    };
    return request;
  }

  auto MakePhysicsSidecarRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.job_name = "DemoScene.physics";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "/.cooked/Scenes/DemoScene.oscene",
      .inline_bindings_json = R"({"bindings":{}})",
    };
    return request;
  }

  class PhysicsImportJobTest : public ImportServiceTest { };

  NOLINT_TEST_F(PhysicsImportJobTest,
    InlineSidecarWithExistingTargetSceneImportsSuccessfullyAndEmitsOpscene)
  {
    const auto cooked_root = TempDir() / "inline_sidecar_success";
    std::filesystem::create_directories(cooked_root);

    const auto scene_report = Import(MakeSceneDescriptorRequest(cooked_root));
    ASSERT_HAS_VALUE(scene_report);
    ASSERT_TRUE(scene_report->success);

    const auto physics_report = Import(MakePhysicsSidecarRequest(cooked_root));
    ASSERT_HAS_VALUE(physics_report);
    EXPECT_TRUE(physics_report->success);
    EXPECT_FALSE(HasDiagnosticCode(
      physics_report->diagnostics, "physics.sidecar.target_scene_missing"));
    EXPECT_FALSE(HasDiagnosticCode(
      physics_report->diagnostics, "physics.sidecar.payload_parse_failed"));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path("Scenes/DemoScene.opscene")));
  }

  NOLINT_TEST_F(PhysicsImportJobTest, InvalidTargetSceneVirtualPathFailsRequest)
  {
    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.cooked_root = TempDir() / "invalid_target_scene_path";
    std::filesystem::create_directories(*request.cooked_root);
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "Scenes/NotCanonical.oscene",
      .inline_bindings_json = R"({"bindings":{"rigid_bodies":[]}})",
    };

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(report->diagnostics,
      "physics.sidecar.target_scene_virtual_path_invalid"));
  }

  NOLINT_TEST_F(
    PhysicsImportJobTest, InvalidInlinePayloadFailsWithParseDiagnostic)
  {
    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.cooked_root = TempDir() / "invalid_inline_payload";
    std::filesystem::create_directories(*request.cooked_root);
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "/Scenes/TestScene.oscene",
      .inline_bindings_json = R"({ invalid payload })",
    };

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "physics.sidecar.payload_parse_failed"));
  }

} // namespace

} // namespace oxygen::content::import::test
