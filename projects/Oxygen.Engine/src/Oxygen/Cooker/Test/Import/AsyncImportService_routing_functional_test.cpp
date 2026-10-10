//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp,
//   Import/Internal/Jobs/MaterialDescriptorImportJob.cpp,
//   Import/Internal/Jobs/SceneDescriptorImportJob.cpp,
//   Import/Internal/Jobs/PhysicsSidecarImportJob.cpp

#include <algorithm>
#include <filesystem>
#include <latch>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;

namespace {

using oxygen::cooker::test::HasDiagnosticCode;

auto StopService(AsyncImportService& service) -> void { service.Stop(); }

//=== Request Routing Tests ===-----------------------------------------------//

class AsyncImportServiceRoutingTest : public oxygen::cooker::test::TempDirTest {
protected:
  AsyncImportService::Config config_ { .thread_pool_size = 2 };
};

NOLINT_TEST_F(
  AsyncImportServiceRoutingTest, PhysicsSidecarRequestBypassesFormatDetection)
{
  AsyncImportService service(config_);

  auto request = ImportRequest {};
  request.source_path = "unrecognized.asset";
  request.cooked_root = TempDir() / "oxygen_physics_routing_submit_test";
  std::filesystem::create_directories(*request.cooked_root);
  request.physics = PhysicsImportSettings {
    .target_scene_virtual_path = "/Scenes/MissingScene.oscene",
    .inline_bindings_json = R"({"bindings":{"rigid_bodies":[]}})",
  };

  std::latch done(1);
  ImportReport report {};
  const auto job_id = service.SubmitImport(
    std::move(request),
    [&report, &done](ImportJobId, const ImportReport& completed) {
      report = completed;
      done.count_down();
    },
    nullptr);

  ASSERT_TRUE(job_id.has_value());
  done.wait();

  EXPECT_FALSE(report.success);
  EXPECT_TRUE(std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
    [](const ImportDiagnostic& diagnostic) {
      return diagnostic.code.starts_with("physics.sidecar.");
    }));

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceRoutingTest,
  MaterialDescriptorRequestBypassesFormatDetection)
{
  AsyncImportService service(config_);

  auto request = ImportRequest {};
  request.source_path = "unrecognized.asset";
  request.cooked_root
    = TempDir() / "oxygen_material_descriptor_routing_submit_test";
  std::filesystem::create_directories(*request.cooked_root);
  request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
    .normalized_descriptor_json
    = R"({"name":"BadMat","textures":{"base_color":{"virtual_path":"not/canonical"}}})",
  };

  std::latch done(1);
  ImportReport report {};
  const auto job_id = service.SubmitImport(
    std::move(request),
    [&report, &done](ImportJobId, const ImportReport& completed) {
      report = completed;
      done.count_down();
    },
    nullptr);

  ASSERT_TRUE(job_id.has_value());
  done.wait();

  EXPECT_FALSE(report.success);
  EXPECT_TRUE(std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
    [](const ImportDiagnostic& diagnostic) {
      return diagnostic.code.starts_with("material.descriptor.");
    }));

  StopService(service);
}

NOLINT_TEST_F(AsyncImportServiceRoutingTest,
  MaterialDescriptorInvalidSchemaProducesDiagnostics)
{
  AsyncImportService service(config_);

  auto request = ImportRequest {};
  request.source_path = "unrecognized.asset";
  request.cooked_root
    = TempDir() / "oxygen_material_descriptor_schema_submit_test";
  std::filesystem::create_directories(*request.cooked_root);
  request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
    .normalized_descriptor_json
    = R"({"name":"BadMat","textures":{"base_color":{"virtual_path":42}}})",
  };

  std::latch done(1);
  ImportReport report {};
  const auto job_id = service.SubmitImport(
    std::move(request),
    [&report, &done](ImportJobId, const ImportReport& completed) {
      report = completed;
      done.count_down();
    },
    nullptr);

  ASSERT_TRUE(job_id.has_value());
  done.wait();

  EXPECT_FALSE(report.success);
  EXPECT_TRUE(HasDiagnosticCode(
    report.diagnostics, "material.descriptor.schema_validation_failed"));

  StopService(service);
}

NOLINT_TEST_F(
  AsyncImportServiceRoutingTest, BufferContainerRequestBypassesFormatDetection)
{
  AsyncImportService service(config_);

  auto request = ImportRequest {};
  request.source_path = "unrecognized.asset";
  request.cooked_root
    = TempDir() / "oxygen_buffer_container_routing_submit_test";
  std::filesystem::create_directories(*request.cooked_root);
  request.buffer_container = ImportRequest::BufferContainerPayload {
    .normalized_descriptor_json
    = R"({"name":"BadContainer","buffers":[{"source":"missing.buffer.bin","virtual_path":"not/canonical","element_stride":16}]})",
  };

  std::latch done(1);
  ImportReport report {};
  const auto job_id = service.SubmitImport(
    std::move(request),
    [&report, &done](ImportJobId, const ImportReport& completed) {
      report = completed;
      done.count_down();
    },
    nullptr);

  ASSERT_TRUE(job_id.has_value());
  done.wait();

  EXPECT_FALSE(report.success);
  EXPECT_TRUE(std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
    [](const ImportDiagnostic& diagnostic) {
      return diagnostic.code.starts_with("buffer.container.");
    }));

  StopService(service);
}

NOLINT_TEST_F(
  AsyncImportServiceRoutingTest, SceneDescriptorRequestBypassesFormatDetection)
{
  AsyncImportService service(config_);

  auto request = ImportRequest {};
  request.source_path = "unrecognized.asset";
  request.cooked_root
    = TempDir() / "oxygen_scene_descriptor_routing_submit_test";
  std::filesystem::create_directories(*request.cooked_root);
  request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
    .normalized_descriptor_json
    = R"({"name":"BadScene","nodes":[{"name":"Root"},{"name":"Mesh","parent":0}],"renderables":[{"node":1,"geometry_ref":"not/canonical"}]})",
  };

  std::latch done(1);
  ImportReport report {};
  const auto job_id = service.SubmitImport(
    std::move(request),
    [&report, &done](ImportJobId, const ImportReport& completed) {
      report = completed;
      done.count_down();
    },
    nullptr);

  ASSERT_TRUE(job_id.has_value());
  done.wait();

  EXPECT_FALSE(report.success);
  EXPECT_TRUE(std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
    [](const ImportDiagnostic& diagnostic) {
      return diagnostic.code.starts_with("scene.descriptor.");
    }));

  StopService(service);
}

} // namespace
