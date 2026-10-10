//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/PhysicsMaterialDescriptorImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/ImportHarness.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;

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

  class PhysicsMaterialDescriptorImportJobTest : public ImportServiceTest { };

  NOLINT_TEST_F(
    PhysicsMaterialDescriptorImportJobTest, SuccessfulJobEmitsMaterialAsset)
  {
    const auto cooked_root = TempDir() / "emits_material";
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

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(report->success);

    constexpr auto kRelPath
      = std::string_view { "Physics/Materials/ground.opmat" };
    const auto has_output = [&](const std::string_view relpath) -> bool {
      return std::ranges::any_of(
        report->outputs, [&](const ImportOutputRecord& output) -> bool {
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

  NOLINT_TEST_F(PhysicsMaterialDescriptorImportJobTest,
    InvalidSchemaPayloadProducesDiagnostic)
  {
    const auto cooked_root = TempDir() / "invalid_schema_payload";
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

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, "physics.material.schema_validation_failed"));
  }

} // namespace

} // namespace oxygen::content::import::test
