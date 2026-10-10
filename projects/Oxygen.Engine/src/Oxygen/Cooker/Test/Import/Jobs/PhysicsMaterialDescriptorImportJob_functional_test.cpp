//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/PhysicsMaterialDescriptorImportJob.cpp

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <latch>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::ScopedTempDir;

  using oxygen::cooker::test::HasDiagnosticCode;

  auto ReadPhysicsMaterialDesc(const std::vector<std::byte>& bytes)
    -> data::pak::physics::PhysicsMaterialAssetDesc
  {
    auto desc = data::pak::physics::PhysicsMaterialAssetDesc {};
    if (bytes.size() < sizeof(desc)) {
      return desc;
    }
    std::memcpy(&desc, bytes.data(), sizeof(desc));
    return desc;
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

  NOLINT_TEST(
    PhysicsMaterialDescriptorImportJobTest, SuccessfulJobEmitsMaterialAsset)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "emits_material";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto request = ImportRequest {};
    request.source_path = source_root / "ground.material.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = R"({
            "name": "ground",
            "static_friction": 0.95,
            "dynamic_friction": 0.65,
            "restitution": 0.05,
            "density": 1700.0,
            "combine_mode_friction": "max",
            "combine_mode_restitution": "average",
            "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
          })",
        };

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_TRUE(report.success);

    constexpr auto kRelPath
      = std::string_view { "Physics/Materials/ground.opmat" };
    const auto has_output = [&](const std::string_view relpath) {
      return std::ranges::any_of(
        report.outputs, [&](const ImportOutputRecord& output) {
          return output.path == relpath;
        });
    };
    EXPECT_TRUE(has_output(kRelPath));

    const auto material_path = cooked_root / std::filesystem::path { kRelPath };
    ASSERT_TRUE(std::filesystem::exists(material_path));

    const auto bytes = ReadBytes(material_path);
    ASSERT_GE(
      bytes.size(), sizeof(data::pak::physics::PhysicsMaterialAssetDesc));
    const auto desc = ReadPhysicsMaterialDesc(bytes);
    EXPECT_EQ(desc.header.asset_type,
      static_cast<uint8_t>(data::AssetType::kPhysicsMaterial));
    EXPECT_EQ(desc.static_friction, 0.95F);
    EXPECT_EQ(desc.dynamic_friction, 0.65F);
    EXPECT_EQ(desc.restitution, 0.05F);
    EXPECT_EQ(desc.density, 1700.0F);
    EXPECT_EQ(
      desc.combine_mode_friction, data::pak::physics::PhysicsCombineMode::kMax);
    EXPECT_EQ(desc.combine_mode_restitution,
      data::pak::physics::PhysicsCombineMode::kAverage);
  }

  NOLINT_TEST(PhysicsMaterialDescriptorImportJobTest,
    InvalidSchemaPayloadProducesDiagnostic)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "invalid_schema_payload";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";

    auto request = ImportRequest {};
    request.source_path = source_root / "bad.material.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_material_descriptor
      = ImportRequest::PhysicsMaterialDescriptorPayload {
          .normalized_descriptor_json = R"({
            "name": "bad_material",
            "static_friction": 0.5,
            "unexpected": true
          })",
        };

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "physics.material.schema_validation_failed"));
  }

} // namespace

} // namespace oxygen::content::import::test
