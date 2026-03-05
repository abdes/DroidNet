//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::test {

namespace {

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [code](const ImportDiagnostic& diagnostic) {
        return diagnostic.code == code;
      });
  }

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_physics_material_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::vector<std::byte>
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open());
    if (!in.is_open()) {
      return {};
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    auto bytes = std::vector<std::byte>(size);
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(in.good() || in.eof());
    return bytes;
  }

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
    const auto cooked_root = MakeTempCookedRoot("emits_material");
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

    const auto bytes = ReadBinaryFile(material_path);
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
    const auto cooked_root = MakeTempCookedRoot("invalid_schema_payload");
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
