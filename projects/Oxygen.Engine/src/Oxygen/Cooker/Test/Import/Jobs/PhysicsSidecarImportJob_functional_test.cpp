//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/PhysicsSidecarImportJob.cpp

#include <algorithm>
#include <filesystem>
#include <latch>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::cooker::test::ScopedTempDir;

  using oxygen::cooker::test::HasDiagnosticCode;

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);

    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) -> void {
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

  NOLINT_TEST(PhysicsImportJobTest,
    InlineSidecarWithExistingTargetSceneImportsSuccessfullyAndEmitsOpscene)
  {
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "inline_sidecar_success";
    std::filesystem::create_directories(cooked_root);

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
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });

    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    const ScopedTempDir temp;
    request.cooked_root = temp.Path() / "invalid_target_scene_path";
    std::filesystem::create_directories(*request.cooked_root);
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
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });

    auto request = ImportRequest {};
    request.source_path = "inline://physics-sidecar";
    const ScopedTempDir temp;
    request.cooked_root = temp.Path() / "invalid_inline_payload";
    std::filesystem::create_directories(*request.cooked_root);
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
