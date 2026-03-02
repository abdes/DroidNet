//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <latch>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>

namespace oxygen::content::import::test {

namespace {

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_scene_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view text) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
  }

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(diagnostics,
      [code](const ImportDiagnostic& d) { return d.code == code; });
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
    if (!submitted.has_value()) {
      report.success = false;
      report.diagnostics.push_back({
        .severity = ImportSeverity::kError,
        .code = "test.submit_failed",
        .message = "Failed to submit scene descriptor import job",
        .source_path = {},
        .object_path = {},
      });
      return report;
    }
    done.wait();
    return report;
  }

  class SceneDescriptorImportJobTest : public testing::Test {
  protected:
    auto MakeRequest(const std::filesystem::path& cooked_root,
      std::string descriptor_json, std::string_view job_name = "DemoScene")
      -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = "inline://scene-descriptor";
      request.job_name = std::string(job_name);
      request.cooked_root = cooked_root;
      request.loose_cooked_layout.virtual_mount_root = "/.cooked";
      request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .normalized_descriptor_json = std::move(descriptor_json),
      };
      return request;
    }
  };

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, ResolvesReferencesAndEmitsSceneDescriptor)
  {
    const auto cooked_root = MakeTempCookedRoot("resolves_and_emits");
    WriteTextFile(cooked_root / "Geometry" / "cube.ogeo", "ogeo");
    WriteTextFile(cooked_root / "Materials" / "cube.omat", "omat");
    WriteTextFile(cooked_root / "Scripts" / "spin.oscript", "oscript");
    WriteTextFile(cooked_root / "Input" / "jump.oiact", "oiact");
    WriteTextFile(cooked_root / "Input" / "gameplay.oimap", "oimap");
    WriteTextFile(cooked_root / "Scenes" / "DemoScene.opscene", "physics");

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "name": "DemoScene",
      "nodes": [
        { "name": "Root" },
        { "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        {
          "node": 1,
          "geometry_ref": "/.cooked/Geometry/cube.ogeo",
          "material_ref": "/.cooked/Materials/cube.omat",
          "visible": true
        }
      ],
      "references": {
        "scripts": ["/.cooked/Scripts/spin.oscript"],
        "input_actions": ["/.cooked/Input/jump.oiact"],
        "input_mapping_contexts": ["/.cooked/Input/gameplay.oimap"],
        "physics_sidecars": ["/.cooked/Scenes/DemoScene.opscene"]
      }
    })"));

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.scenes_written, 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.reference_missing"));

    auto layout = LooseCookedLayout {};
    const auto expected_relpath = layout.SceneDescriptorRelPath("DemoScene");
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path(expected_relpath)));

    service.Stop();
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, MissingGeometryReferenceProducesDiagnostic)
  {
    const auto cooked_root = MakeTempCookedRoot("missing_geometry_reference");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "name": "DemoScene",
      "nodes": [
        { "name": "Root" },
        { "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        {
          "node": 1,
          "geometry_ref": "/.cooked/Geometry/missing.ogeo"
        }
      ]
    })"));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.reference_missing"));

    service.Stop();
  }

} // namespace

} // namespace oxygen::content::import::test
