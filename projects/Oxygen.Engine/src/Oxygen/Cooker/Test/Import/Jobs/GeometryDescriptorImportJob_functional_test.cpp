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
#include <utility>
#include <vector>

#include "GeometryDescriptorImportJobTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_render.h>
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

  const auto descriptor_doc = MakeStandardDescriptorDoc("CubeMissingMaterial",
    "/.cooked/Materials/missing.omat",
    "/.cooked/Resources/Buffers/cube_vertices.obuf",
    "/.cooked/Resources/Buffers/cube_indices.obuf", "lod0",
    json::array({
      json {
        { "uri", vb_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_vertices.obuf" },
        { "usage_flags", 1U },
        { "element_stride", 32U },
        {
          "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }),
        },
      },
      json {
        { "uri", ib_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_indices.obuf" },
        { "usage_flags", 2U },
        { "element_stride", 4U },
        {
          "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }),
        },
      },
    }));
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_FALSE(report.success);
  EXPECT_TRUE(
    HasDiagnosticCode(report.diagnostics, "geometry.material.missing"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  NewMaterialRespectsPriorityBeforeIndexPublication)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "new_material_priority";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "priority.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  const auto descriptor_doc = MakeStandardDescriptorDoc("NewMaterialPriority",
    "/.cooked/Materials/new.omat",
    "/.cooked/Resources/Buffers/cube_vertices.obuf",
    "/.cooked/Resources/Buffers/cube_indices.obuf", "lod0",
    json::array({
      json {
        { "uri", vb_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_vertices.obuf" },
        { "usage_flags", 1U },
        { "element_stride", 32U },
        {
          "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }),
        },
      },
      json {
        { "uri", ib_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_indices.obuf" },
        { "usage_flags", 2U },
        { "element_stride", 4U },
        {
          "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }),
        },
      },
    }));
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto library = root / "Library";
  const auto library_key = data::AssetKey::FromVirtualPath("/Library/New.omat");
  auto writer = LooseCookedWriter(library);
  auto material_desc = data::pak::render::MaterialAssetDesc {};
  material_desc.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kMaterial);
  material_desc.header.version = data::pak::render::kMaterialAssetVersion;
  writer.WriteAssetDescriptor(library_key, data::AssetType::kMaterial,
    "/.cooked/Materials/new.omat", "Materials/new.omat",
    std::as_bytes(std::span(&material_desc, 1)), {});
  static_cast<void>(writer.Finish());
  WriteText(cooked_root / "Materials/new.omat", "new material");
  for (const auto own_wins : { true, false }) {
    auto request
      = MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc);
    request.cooked_context_roots = own_wins
      ? std::vector<std::filesystem::path> { library, cooked_root }
      : std::vector<std::filesystem::path> { cooked_root, library };
    const auto report = SubmitAndWait(service, std::move(request));
    ASSERT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
    const auto descriptor_bytes
      = ReadBytes(cooked_root / "Geometry/NewMaterialPriority.ogeo");
    const auto offset = sizeof(data::pak::geometry::GeometryAssetDesc)
      + sizeof(data::pak::geometry::MeshDesc);
    const auto submesh = ReadStructAt<data::pak::geometry::SubMeshDesc>(
      descriptor_bytes, offset);
    EXPECT_EQ(submesh.material_asset_key,
      own_wins ? data::AssetKey::FromVirtualPath("/.cooked/Materials/new.omat")
               : library_key);
  }
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

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

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

  const auto descriptor_doc = json {
    { "name", "SkinnedCube" },
    { "bounds", MakeBounds() },
    {
      "buffers",
      json::array({
        json {
          { "uri", vb_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/skinned_vb.obuf" },
          { "usage_flags", 1U },
          { "element_stride", 32U },
          {
            "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }),
          },
        },
        json {
          { "uri", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/skinned_ib.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          {
            "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }),
          },
        },
        json {
          { "uri", joint_index_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/skinned_joint_index.obuf",
          },
          { "usage_flags", 8U },
          { "element_stride", 16U },
        },
        json {
          { "uri", joint_weight_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/skinned_joint_weight.obuf",
          },
          { "usage_flags", 8U },
          { "element_stride", 16U },
        },
        json {
          { "uri", inverse_bind_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/skinned_inverse_bind.obuf",
          },
          { "usage_flags", 8U },
          { "element_stride", 64U },
        },
        json {
          { "uri", joint_remap_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/skinned_joint_remap.obuf",
          },
          { "usage_flags", 8U },
          { "element_stride", 4U },
        },
      }),
    },
    {
      "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "skinned" },
          { "bounds", MakeBounds() },
          {
            "buffers",
            {
              { "vb_ref", "/.cooked/Resources/Buffers/skinned_vb.obuf" },
              { "ib_ref", "/.cooked/Resources/Buffers/skinned_ib.obuf" },
            },
          },
          {
            "skinning",
            {
              {
                "joint_index_ref",
                "/.cooked/Resources/Buffers/skinned_joint_index.obuf",
              },
              {
                "joint_weight_ref",
                "/.cooked/Resources/Buffers/skinned_joint_weight.obuf",
              },
              {
                "inverse_bind_ref",
                "/.cooked/Resources/Buffers/skinned_inverse_bind.obuf",
              },
              {
                "joint_remap_ref",
                "/.cooked/Resources/Buffers/skinned_joint_remap.obuf",
              },
              { "joint_count", 3U },
              { "influences_per_vertex", 4U },
            },
          },
          {
            "submeshes",
            json::array({
              json {
                { "slot_id", kAuthoredSlotId },
                { "material_ref", "/.cooked/Materials/default.omat" },
                { "views", json::array({ json { { "view_ref", "lod0" } } }) },
              },
            }),
          },
        },
      }),
    },
  };
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  ASSERT_TRUE(report.success)
    << oxygen::cooker::test::DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value())
    << "Expected geometry_relpath to contain a value";

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

} // namespace oxygen::content::import::test
