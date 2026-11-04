//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/GeometryDescriptorImportJob.cpp,
//   Import/Internal/Jobs/BufferContainerImportJob.cpp

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "GeometryDescriptorImportJobTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

NOLINT_TEST(GeometryDescriptorImportJobTest,
  MissingMaterialReferenceFailsWithHelpfulDiagnostic)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "missing_material_reference";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "missing_material.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  constexpr auto kVb = "/Cooked/Resources/Buffers/cube_vertices.obuf";
  constexpr auto kIb = "/Cooked/Resources/Buffers/cube_indices.obuf";
  auto geometry
    = MakeStandardGeometry("/Cooked/Geometry/CubeMissingMaterial.ogeo",
      "/Cooked/Materials/missing.omat", kVb, kIb);
  geometry
    .AppendBuffer(
      MakeBuffer(vb_source.filename().generic_string(), 1U, 32U, true), kVb)
    .AppendBuffer(
      MakeBuffer(ib_source.filename().generic_string(), 2U, 4U, true), kIb);
  WriteText(descriptor_path, geometry.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = descriptor_path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry.Json(),
    }));
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryMaterialMissing));
}

//! Only an index lists an asset: a material file no index lists, even in the
//! root of highest priority, is none, and the indexed library material wins.
NOLINT_TEST(GeometryDescriptorImportJobTest, UnindexedMaterialFileIsNoAsset)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "unindexed_material";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "unindexed.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  constexpr auto kVb = "/Cooked/Resources/Buffers/cube_vertices.obuf";
  constexpr auto kIb = "/Cooked/Resources/Buffers/cube_indices.obuf";
  auto geometry = MakeStandardGeometry(
    "/Cooked/Geometry/Unindexed.ogeo", "/Cooked/Materials/new.omat", kVb, kIb);
  geometry
    .AppendBuffer(
      MakeBuffer(vb_source.filename().generic_string(), 1U, 32U, true), kVb)
    .AppendBuffer(
      MakeBuffer(ib_source.filename().generic_string(), 2U, 4U, true), kIb);
  WriteText(descriptor_path, geometry.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto library = root / "Library";
  const auto library_key = data::AssetKey::FromVirtualPath("/Library/New.omat");
  auto writer
    = LooseCookedWriter(library, oxygen::content::cooked::RealCookerStorage());
  const auto material = oxygen::cooker::testing::MaterialBuilder {};
  writer.WriteAssetDescriptor(library_key, data::AssetType::kMaterial,
    "/Cooked/Materials/new.omat", "Materials/new.omat", material.Bytes(),
    material.References(),
    content::import::LooseCookedProducer {
      .source = "/Cooked/Materials/new.omat.json",
      .completes = {},
    });
  static_cast<void>(writer.Finish());
  WriteText(cooked_root / "Materials/new.omat", "stray material");

  auto request = MakeGeometryRequest({
    .source_path = descriptor_path,
    .cooked_root = cooked_root,
    .descriptor_doc = geometry.Json(),
  });
  request.cooked_context_roots
    = std::vector<std::filesystem::path> { library, cooked_root };
  const auto report = SubmitAndWait(service, std::move(request));
  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
  const auto descriptor_bytes = ReadBytes(
    oxygen::cooker::test::CookedFile(cooked_root, "Geometry/Unindexed.ogeo"));
  const auto offset = sizeof(data::pak::geometry::GeometryAssetDesc)
    + sizeof(data::pak::geometry::MeshDesc);
  const auto submesh = oxygen::serio::CopyAt<data::pak::geometry::SubMeshDesc>(
    std::span { descriptor_bytes }, oxygen::serio::ByteOffset { offset });
  ASSERT_HAS_VALUE(submesh);
  EXPECT_EQ(submesh->material_asset_key, library_key);
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  SkinnedDescriptorWithLocalBuffersImportsSuccessfully)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "skinned_local_buffers_success";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "skinned.geometry.json";

  const auto vb_source = source_dir / "skinned.vertices.buffer.bin";
  const auto ib_source = source_dir / "skinned.indices.buffer.bin";
  const auto joint_index_source
    = source_dir / "skinned.joint_indices.buffer.bin";
  const auto joint_weight_source
    = source_dir / "skinned.joint_weights.buffer.bin";
  const auto inverse_bind_source
    = source_dir / "skinned.inverse_bind.buffer.bin";
  const auto joint_remap_source = source_dir / "skinned.joint_remap.buffer.bin";

  CookDefaultMaterial(cooked_root);

  auto vb_bytes = std::array<std::byte, 96> {};
  auto ib_bytes = std::array<std::byte, 12> {};
  auto joint_index_bytes = std::array<std::byte, 48> {};
  auto joint_weight_bytes = std::array<std::byte, 48> {};
  auto inverse_bind_bytes = std::array<std::byte, 192> {};
  auto joint_remap_bytes = std::array<std::byte, 12> {};
  for (size_t i = 0; i < vb_bytes.size(); ++i) {
    vb_bytes.at(i) = static_cast<std::byte>((i + 1U) & 0xFFU);
  }
  for (size_t i = 0; i < ib_bytes.size(); ++i) {
    ib_bytes.at(i) = static_cast<std::byte>(((i * 3U) + 7U) & 0xFFU);
  }
  for (size_t i = 0; i < joint_index_bytes.size(); ++i) {
    joint_index_bytes.at(i) = static_cast<std::byte>(((i * 5U) + 11U) & 0xFFU);
  }
  for (size_t i = 0; i < joint_weight_bytes.size(); ++i) {
    joint_weight_bytes.at(i) = static_cast<std::byte>(((i * 7U) + 13U) & 0xFFU);
  }
  for (size_t i = 0; i < inverse_bind_bytes.size(); ++i) {
    inverse_bind_bytes.at(i) = static_cast<std::byte>(((i * 9U) + 17U) & 0xFFU);
  }
  for (size_t i = 0; i < joint_remap_bytes.size(); ++i) {
    joint_remap_bytes.at(i) = static_cast<std::byte>(((i * 11U) + 19U) & 0xFFU);
  }
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));
  WriteBytes(joint_index_source, std::span<const std::byte>(joint_index_bytes));
  WriteBytes(
    joint_weight_source, std::span<const std::byte>(joint_weight_bytes));
  WriteBytes(
    inverse_bind_source, std::span<const std::byte>(inverse_bind_bytes));
  WriteBytes(joint_remap_source, std::span<const std::byte>(joint_remap_bytes));

  constexpr auto kPrefix = "/Cooked/Resources/Buffers/skinned_";
  const auto buffer = [](const std::string_view leaf) -> std::string {
    return std::string { kPrefix } + std::string { leaf } + ".obuf";
  };
  auto geometry = MakeStandardGeometry("/Cooked/Geometry/SkinnedCube.ogeo",
    "/Cooked/Materials/default.omat", buffer("vb"), buffer("ib"));
  geometry.Set("/lods/0/mesh_type", "skinned")
    .Reference("/lods/0/skinning/joint_index_ref", buffer("joint_index"))
    .Reference("/lods/0/skinning/joint_weight_ref", buffer("joint_weight"))
    .Reference("/lods/0/skinning/inverse_bind_ref", buffer("inverse_bind"))
    .Reference("/lods/0/skinning/joint_remap_ref", buffer("joint_remap"))
    .Set("/lods/0/skinning/joint_count", 3U)
    .Set("/lods/0/skinning/influences_per_vertex", 4U)
    .AppendBuffer(
      MakeBuffer(vb_source.filename().generic_string(), 1U, 32U, true),
      buffer("vb"))
    .AppendBuffer(
      MakeBuffer(ib_source.filename().generic_string(), 2U, 4U, true),
      buffer("ib"))
    .AppendBuffer(MakeBuffer(joint_index_source.filename().generic_string(), 8U,
                    16U, false),
      buffer("joint_index"))
    .AppendBuffer(MakeBuffer(joint_weight_source.filename().generic_string(),
                    8U, 16U, false),
      buffer("joint_weight"))
    .AppendBuffer(MakeBuffer(inverse_bind_source.filename().generic_string(),
                    8U, 64U, false),
      buffer("inverse_bind"))
    .AppendBuffer(
      MakeBuffer(joint_remap_source.filename().generic_string(), 8U, 4U, false),
      buffer("joint_remap"));
  WriteText(descriptor_path, geometry.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = descriptor_path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry.Json(),
    }));
  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success)
    << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
  EXPECT_EQ(report->geometry_written, 1U)
    << DiagnosticSummary(report->diagnostics);

  const auto geometry_relpath = FindOutputByExtension(*report, ".ogeo");
  ASSERT_HAS_VALUE(geometry_relpath)
    << "Expected geometry_relpath to contain a value";

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

} // namespace oxygen::content::import::test
