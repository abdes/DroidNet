//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <latch>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>

namespace oxygen::content::import::test {

namespace {

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(diagnostics,
      [code](const ImportDiagnostic& d) { return d.code == code; });
  }

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root
      = std::filesystem::temp_directory_path() / "oxygen_physics_import";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);

    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) {
        report = completed;
        done.count_down();
      },
      nullptr);
    EXPECT_TRUE(submitted.has_value());
    done.wait();
    return report;
  }

  auto MakeSceneDescriptorRequest(const std::filesystem::path& cooked_root)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = "inline://scene-descriptor";
    request.job_name = "DemoScene";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
      .normalized_descriptor_json
      = R"({"name":"DemoScene","nodes":[{"name":"Root"}]})",
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

  NOLINT_TEST(PhysicsImportJobTest,
    InlineSidecarWithExistingTargetSceneImportsSuccessfullyAndEmitsOpscene)
  {
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    const auto cooked_root = MakeTempCookedRoot("inline_sidecar_success");

    const auto scene_report
      = SubmitAndWait(service, MakeSceneDescriptorRequest(cooked_root));
    ASSERT_TRUE(scene_report.success);

    const auto physics_report
      = SubmitAndWait(service, MakePhysicsSidecarRequest(cooked_root));
    EXPECT_TRUE(physics_report.success);
    EXPECT_FALSE(HasDiagnosticCode(
      physics_report.diagnostics, "physics.sidecar.target_scene_missing"));
    EXPECT_FALSE(HasDiagnosticCode(
      physics_report.diagnostics, "physics.sidecar.payload_parse_failed"));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path("Scenes/DemoScene.opscene")));

    service.Stop();
  }

  NOLINT_TEST(PhysicsImportJobTest, InvalidTargetSceneVirtualPathFailsRequest)
  {
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.cooked_root = MakeTempCookedRoot("invalid_target_scene_path");
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "Scenes/NotCanonical.oscene",
      .inline_bindings_json = R"({"bindings":{"rigid_bodies":[]}})",
    };

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "physics.sidecar.target_scene_virtual_path_invalid"));

    service.Stop();
  }

  NOLINT_TEST(
    PhysicsImportJobTest, InvalidInlinePayloadFailsWithParseDiagnostic)
  {
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    request.cooked_root = MakeTempCookedRoot("invalid_inline_payload");
    request.physics = PhysicsImportSettings {
      .target_scene_virtual_path = "/Scenes/TestScene.oscene",
      .inline_bindings_json = R"({ invalid payload })",
    };

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "physics.sidecar.payload_parse_failed"));

    service.Stop();
  }

} // namespace

} // namespace oxygen::content::import::test
