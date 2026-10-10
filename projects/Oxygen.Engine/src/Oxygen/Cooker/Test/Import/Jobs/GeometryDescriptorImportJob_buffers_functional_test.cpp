//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/GeometryDescriptorImportJob.cpp,
//   Import/Internal/Jobs/BufferContainerImportJob.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "GeometryDescriptorImportJobTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  auto MakeBufferContainerRequest(const std::filesystem::path& source_path,
    const std::filesystem::path& cooked_root, const json& descriptor_doc)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = source_path.lexically_normal();
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_doc.dump(),
    };
    return request;
  }

  //! Authored inputs shared by the buffer sidecar tests. Each cooked root is
  //! produced from the same sources and differs only by its descriptor.
  struct LocalBufferSources final {
    std::filesystem::path root;
    std::filesystem::path cooked_root;
    std::filesystem::path buffer_manifest_path;
    std::filesystem::path geometry_path;
    std::filesystem::path vb_source;
    std::array<std::byte, 96> vb_bytes {};
    json buffer_descriptor;
    json geometry_descriptor;
  };

  auto WriteLocalBufferSources(const std::filesystem::path& root)
    -> LocalBufferSources
  {
    std::filesystem::create_directories(root);
    const auto source_dir = root / "Sources";
    const auto cooked_root = root / ".cooked";
    const auto buffer_manifest_path = source_dir / "shared.buffers.json";
    const auto geometry_path = source_dir / "shared_ref.geometry.json";
    const auto vb_source = source_dir / "shared.vertices.buffer.bin";
    const auto ib_source = source_dir / "shared.indices.buffer.bin";

    std::filesystem::create_directories(cooked_root / "Materials");
    WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

    const auto vb_bytes = std::array<std::byte, 96> {
      std::byte { 0x00 },
      std::byte { 0x01 },
      std::byte { 0x02 },
      std::byte { 0x03 },
      std::byte { 0x04 },
      std::byte { 0x05 },
      std::byte { 0x06 },
      std::byte { 0x07 },
      std::byte { 0x08 },
      std::byte { 0x09 },
      std::byte { 0x0A },
      std::byte { 0x0B },
      std::byte { 0x0C },
      std::byte { 0x0D },
      std::byte { 0x0E },
      std::byte { 0x0F },
      std::byte { 0x10 },
      std::byte { 0x11 },
      std::byte { 0x12 },
      std::byte { 0x13 },
      std::byte { 0x14 },
      std::byte { 0x15 },
      std::byte { 0x16 },
      std::byte { 0x17 },
      std::byte { 0x18 },
      std::byte { 0x19 },
      std::byte { 0x1A },
      std::byte { 0x1B },
      std::byte { 0x1C },
      std::byte { 0x1D },
      std::byte { 0x1E },
      std::byte { 0x1F },
      std::byte { 0x20 },
      std::byte { 0x21 },
      std::byte { 0x22 },
      std::byte { 0x23 },
      std::byte { 0x24 },
      std::byte { 0x25 },
      std::byte { 0x26 },
      std::byte { 0x27 },
      std::byte { 0x28 },
      std::byte { 0x29 },
      std::byte { 0x2A },
      std::byte { 0x2B },
      std::byte { 0x2C },
      std::byte { 0x2D },
      std::byte { 0x2E },
      std::byte { 0x2F },
      std::byte { 0x30 },
      std::byte { 0x31 },
      std::byte { 0x32 },
      std::byte { 0x33 },
      std::byte { 0x34 },
      std::byte { 0x35 },
      std::byte { 0x36 },
      std::byte { 0x37 },
      std::byte { 0x38 },
      std::byte { 0x39 },
      std::byte { 0x3A },
      std::byte { 0x3B },
      std::byte { 0x3C },
      std::byte { 0x3D },
      std::byte { 0x3E },
      std::byte { 0x3F },
      std::byte { 0x40 },
      std::byte { 0x41 },
      std::byte { 0x42 },
      std::byte { 0x43 },
      std::byte { 0x44 },
      std::byte { 0x45 },
      std::byte { 0x46 },
      std::byte { 0x47 },
      std::byte { 0x48 },
      std::byte { 0x49 },
      std::byte { 0x4A },
      std::byte { 0x4B },
      std::byte { 0x4C },
      std::byte { 0x4D },
      std::byte { 0x4E },
      std::byte { 0x4F },
      std::byte { 0x50 },
      std::byte { 0x51 },
      std::byte { 0x52 },
      std::byte { 0x53 },
      std::byte { 0x54 },
      std::byte { 0x55 },
      std::byte { 0x56 },
      std::byte { 0x57 },
      std::byte { 0x58 },
      std::byte { 0x59 },
      std::byte { 0x5A },
      std::byte { 0x5B },
      std::byte { 0x5C },
      std::byte { 0x5D },
      std::byte { 0x5E },
      std::byte { 0x5F },
    };
    const auto ib_bytes = std::array<std::byte, 12> {
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x01 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x02 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
    };
    WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
    WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

    const auto buffer_descriptor = json {
      { "name", "SharedBuffers" },
      {
        "buffers",
        json::array({
          json {
            { "source", vb_source.generic_string() },
            {
              "virtual_path",
              "/.cooked/Resources/Buffers/shared_vertices.obuf",
            },
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
            { "source", ib_source.generic_string() },
            {
              "virtual_path",
              "/.cooked/Resources/Buffers/shared_indices.obuf",
            },
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
        }),
      },
    };
    WriteText(buffer_manifest_path, buffer_descriptor.dump(2));

    const auto geometry_descriptor = MakeStandardDescriptorDoc("SharedCube",
      "/.cooked/Materials/default.omat",
      "/.cooked/Resources/Buffers/shared_vertices.obuf",
      "/.cooked/Resources/Buffers/shared_indices.obuf", "lod0", std::nullopt);
    WriteText(geometry_path, geometry_descriptor.dump(2));

    return LocalBufferSources {
      .root = root,
      .cooked_root = cooked_root,
      .buffer_manifest_path = buffer_manifest_path,
      .geometry_path = geometry_path,
      .vb_source = vb_source,
      .vb_bytes = vb_bytes,
      .buffer_descriptor = buffer_descriptor,
      .geometry_descriptor = geometry_descriptor,
    };
  }

  //! Cooks the buffer manifest `descriptor` into `cooked_root`.
  auto CookLocalBuffers(AsyncImportService& service,
    const LocalBufferSources& sources, const std::filesystem::path& cooked_root,
    const json& descriptor) -> ImportReport
  {
    const auto report = SubmitAndWait(service,
      MakeBufferContainerRequest(
        sources.buffer_manifest_path, cooked_root, descriptor));
    if (!report.has_value()) {
      return ImportReport {};
    }
    return *report;
  }

} // namespace

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  LocalBuffersEmitGeometryAndBufferArtifacts)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "local_buffers_emit_artifacts";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "cube.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x02 },
    std::byte { 0x03 },
    std::byte { 0x04 },
    std::byte { 0x05 },
    std::byte { 0x06 },
    std::byte { 0x07 },
    std::byte { 0x08 },
    std::byte { 0x09 },
    std::byte { 0x0A },
    std::byte { 0x0B },
    std::byte { 0x0C },
    std::byte { 0x0D },
    std::byte { 0x0E },
    std::byte { 0x0F },
    std::byte { 0x10 },
    std::byte { 0x11 },
    std::byte { 0x12 },
    std::byte { 0x13 },
    std::byte { 0x14 },
    std::byte { 0x15 },
    std::byte { 0x16 },
    std::byte { 0x17 },
    std::byte { 0x18 },
    std::byte { 0x19 },
    std::byte { 0x1A },
    std::byte { 0x1B },
    std::byte { 0x1C },
    std::byte { 0x1D },
    std::byte { 0x1E },
    std::byte { 0x1F },
    std::byte { 0x20 },
    std::byte { 0x21 },
    std::byte { 0x22 },
    std::byte { 0x23 },
    std::byte { 0x24 },
    std::byte { 0x25 },
    std::byte { 0x26 },
    std::byte { 0x27 },
    std::byte { 0x28 },
    std::byte { 0x29 },
    std::byte { 0x2A },
    std::byte { 0x2B },
    std::byte { 0x2C },
    std::byte { 0x2D },
    std::byte { 0x2E },
    std::byte { 0x2F },
    std::byte { 0x30 },
    std::byte { 0x31 },
    std::byte { 0x32 },
    std::byte { 0x33 },
    std::byte { 0x34 },
    std::byte { 0x35 },
    std::byte { 0x36 },
    std::byte { 0x37 },
    std::byte { 0x38 },
    std::byte { 0x39 },
    std::byte { 0x3A },
    std::byte { 0x3B },
    std::byte { 0x3C },
    std::byte { 0x3D },
    std::byte { 0x3E },
    std::byte { 0x3F },
    std::byte { 0x40 },
    std::byte { 0x41 },
    std::byte { 0x42 },
    std::byte { 0x43 },
    std::byte { 0x44 },
    std::byte { 0x45 },
    std::byte { 0x46 },
    std::byte { 0x47 },
    std::byte { 0x48 },
    std::byte { 0x49 },
    std::byte { 0x4A },
    std::byte { 0x4B },
    std::byte { 0x4C },
    std::byte { 0x4D },
    std::byte { 0x4E },
    std::byte { 0x4F },
    std::byte { 0x50 },
    std::byte { 0x51 },
    std::byte { 0x52 },
    std::byte { 0x53 },
    std::byte { 0x54 },
    std::byte { 0x55 },
    std::byte { 0x56 },
    std::byte { 0x57 },
    std::byte { 0x58 },
    std::byte { 0x59 },
    std::byte { 0x5A },
    std::byte { 0x5B },
    std::byte { 0x5C },
    std::byte { 0x5D },
    std::byte { 0x5E },
    std::byte { 0x5F },
  };
  const auto ib_bytes = std::array<std::byte, 12> {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x02 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
  };
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  const auto descriptor_doc
    = MakeStandardDescriptorDoc("Cube", "/.cooked/Materials/default.omat",
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
  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success)
    << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
  EXPECT_EQ(report->geometry_written, 1U)
    << DiagnosticSummary(report->diagnostics);
  EXPECT_FALSE(
    HasDiagnosticCode(report->diagnostics, "geometry.material.missing"));

  const auto geometry_relpath = FindOutputByExtension(*report, ".ogeo");
  ASSERT_HAS_VALUE(geometry_relpath)
    << "Expected geometry_relpath to contain a value";
  ASSERT_TRUE(std::filesystem::exists(
    cooked_root / std::filesystem::path(*geometry_relpath)));

  const auto has_output = [&](const std::string_view relpath) -> bool {
    return std::ranges::any_of(
      report->outputs, [&](const ImportOutputRecord& output) -> bool {
        return output.path == relpath;
      });
  };
  EXPECT_TRUE(has_output("Resources/Buffers/cube_vertices.obuf"));
  EXPECT_TRUE(has_output("Resources/Buffers/cube_indices.obuf"));
  EXPECT_TRUE(has_output("Resources/buffers.data"));
  EXPECT_TRUE(has_output("Resources/buffers.table"));

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  ASSERT_GE(descriptor_bytes.size(),
    sizeof(data::pak::geometry::GeometryAssetDesc)
      + sizeof(data::pak::geometry::MeshDesc)
      + sizeof(data::pak::geometry::SubMeshDesc)
      + sizeof(data::pak::geometry::MeshViewDesc));

  auto offset = sizeof(data::pak::geometry::GeometryAssetDesc);
  const auto mesh_desc
    = ReadStructAt<data::pak::geometry::MeshDesc>(descriptor_bytes, offset);
  EXPECT_EQ(
    mesh_desc.mesh_type, static_cast<uint8_t>(data::MeshType::kStandard));
  EXPECT_NE(mesh_desc.info.standard.vertex_buffer, data::kNoResourceReference);
  EXPECT_NE(mesh_desc.info.standard.index_buffer, data::kNoResourceReference);
  EXPECT_EQ(mesh_desc.submesh_count, 1U);
  EXPECT_EQ(mesh_desc.mesh_view_count, 1U);

  offset += sizeof(data::pak::geometry::MeshDesc);
  const auto submesh_desc
    = ReadStructAt<data::pak::geometry::SubMeshDesc>(descriptor_bytes, offset);
  EXPECT_EQ(submesh_desc.material_asset_key,
    oxygen::data::AssetKey::FromVirtualPath("/.cooked/Materials/default.omat"));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, ResolvesLocalBufferSidecars)
{
  const ScopedTempDir temp;
  const auto sources
    = WriteLocalBufferSources(temp.Path() / "pre_cooked_buffer_sidecars");
  const auto& cooked_root = sources.cooked_root;
  const auto& geometry_path = sources.geometry_path;
  const auto& geometry_descriptor = sources.geometry_descriptor;

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto buffer_report = CookLocalBuffers(
    service, sources, cooked_root, sources.buffer_descriptor);
  ASSERT_TRUE(buffer_report.success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest(geometry_path, cooked_root, geometry_descriptor));
  ASSERT_HAS_VALUE(geometry_report);
  ASSERT_TRUE(geometry_report->success)
    << oxygen::cooker::test::DiagnosticSummary(geometry_report->diagnostics);
  EXPECT_EQ(geometry_report->geometry_written, 1U);
  EXPECT_FALSE(HasDiagnosticCode(
    geometry_report->diagnostics, "geometry.buffer.sidecar_missing"));

  const auto geometry_relpath
    = FindOutputByExtension(*geometry_report, ".ogeo");
  ASSERT_HAS_VALUE(geometry_relpath)
    << "Expected geometry_relpath to contain a value";
  ASSERT_TRUE(std::filesystem::exists(
    cooked_root / std::filesystem::path(*geometry_relpath)));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  RejectsBufferSidecarsOwnedByForeignRoot)
{
  const ScopedTempDir temp;
  const auto sources
    = WriteLocalBufferSources(temp.Path() / "pre_cooked_buffer_sidecars");
  const auto& root = sources.root;
  const auto& cooked_root = sources.cooked_root;
  const auto& geometry_path = sources.geometry_path;
  const auto& geometry_descriptor = sources.geometry_descriptor;
  const auto& vb_source = sources.vb_source;
  const auto& vb_bytes = sources.vb_bytes;

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto buffer_report = CookLocalBuffers(
    service, sources, cooked_root, sources.buffer_descriptor);
  ASSERT_TRUE(buffer_report.success);

  const auto other_root = root / "other-cooked";
  auto other_descriptor = sources.buffer_descriptor;
  other_descriptor.at("buffers").at(0).at("virtual_path")
    = "/.cooked/Resources/Buffers/other_vertices.obuf";
  other_descriptor.at("buffers").at(1).at("virtual_path")
    = "/.cooked/Resources/Buffers/other_indices.obuf";
  auto other_vertices = vb_bytes;
  other_vertices.front() = std::byte { 0x7F };
  WriteBytes(vb_source, std::span<const std::byte>(other_vertices));
  const auto other_buffers
    = CookLocalBuffers(service, sources, other_root, other_descriptor);
  ASSERT_TRUE(other_buffers.success);

  // Both roots have populated buffer tables, but only the foreign root owns
  // the requested sidecars. Its numeric indices cannot identify local bytes.
  const auto rejected = SubmitAndWait(service,
    MakeGeometryRequest(
      geometry_path, other_root, geometry_descriptor, { cooked_root }));
  ASSERT_HAS_VALUE(rejected);
  EXPECT_FALSE(rejected->success);
  EXPECT_EQ(rejected->geometry_written, 0U);
  EXPECT_TRUE(
    HasDiagnosticCode(rejected->diagnostics, "geometry.buffer.foreign_root"));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  RejectsBufferSidecarsFromAdditionalCookedContextRoot)
{
  const ScopedTempDir temp;
  const auto main_root = temp.Path() / "resolve_from_context_root_main";
  std::filesystem::create_directories(main_root);
  const auto context_root = temp.Path() / "resolve_from_context_root_ctx";
  std::filesystem::create_directories(context_root);
  const auto source_dir = main_root / "Sources";
  const auto cooked_root = main_root / ".cooked";
  const auto context_cooked_root = context_root / ".cooked";
  const auto buffer_manifest_path = source_dir / "context_shared.buffers.json";
  const auto geometry_path = source_dir / "context_ref.geometry.json";
  const auto vb_source = source_dir / "context.vertices.buffer.bin";
  const auto ib_source = source_dir / "context.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  const auto buffer_descriptor = json {
    { "name", "ContextSharedBuffers" },
    {
      "buffers",
      json::array({
        json {
          { "source", vb_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/context_vertices.obuf",
          },
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
          { "source", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/context_indices.obuf" },
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
      }),
    },
  };
  WriteText(buffer_manifest_path, buffer_descriptor.dump(2));

  const auto geometry_descriptor = MakeStandardDescriptorDoc(
    "ContextSharedCube", "/.cooked/Materials/default.omat",
    "/.cooked/Resources/Buffers/context_vertices.obuf",
    "/.cooked/Resources/Buffers/context_indices.obuf", "lod0", std::nullopt);
  WriteText(geometry_path, geometry_descriptor.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto buffer_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, context_cooked_root, buffer_descriptor));
  ASSERT_HAS_VALUE(buffer_report);
  ASSERT_TRUE(buffer_report->success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest(geometry_path, cooked_root, geometry_descriptor,
      { context_cooked_root }));
  ASSERT_HAS_VALUE(geometry_report);
  EXPECT_FALSE(geometry_report->success);
  EXPECT_EQ(geometry_report->geometry_written, 0U);
  EXPECT_TRUE(HasDiagnosticCode(
    geometry_report->diagnostics, "geometry.buffer.foreign_root"));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  DuplicateMountedBufferSidecarsFailWithAmbiguousDiagnostic)
{
  const ScopedTempDir temp;
  const auto main_root = temp.Path() / "ambiguous_sidecar_main";
  std::filesystem::create_directories(main_root);
  const auto context_root = temp.Path() / "ambiguous_sidecar_ctx";
  std::filesystem::create_directories(context_root);
  const auto source_dir = main_root / "Sources";
  const auto cooked_root = main_root / ".cooked";
  const auto context_cooked_root = context_root / ".cooked";
  const auto buffer_manifest_path = source_dir / "ambiguous.buffers.json";
  const auto geometry_path = source_dir / "ambiguous.geometry.json";
  const auto vb_source = source_dir / "ambiguous.vertices.buffer.bin";
  const auto ib_source = source_dir / "ambiguous.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  const auto buffer_descriptor = json {
    { "name", "AmbiguousBuffers" },
    {
      "buffers",
      json::array({
        json {
          { "source", vb_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/ambiguous_vertices.obuf",
          },
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
          { "source", ib_source.generic_string() },
          {
            "virtual_path",
            "/.cooked/Resources/Buffers/ambiguous_indices.obuf",
          },
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
      }),
    },
  };
  WriteText(buffer_manifest_path, buffer_descriptor.dump(2));

  const auto geometry_descriptor = MakeStandardDescriptorDoc("AmbiguousCube",
    "/.cooked/Materials/default.omat",
    "/.cooked/Resources/Buffers/ambiguous_vertices.obuf",
    "/.cooked/Resources/Buffers/ambiguous_indices.obuf", "lod0", std::nullopt);
  WriteText(geometry_path, geometry_descriptor.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto main_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, cooked_root, buffer_descriptor));
  ASSERT_HAS_VALUE(main_report);
  ASSERT_TRUE(main_report->success);
  const auto context_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, context_cooked_root, buffer_descriptor));
  ASSERT_HAS_VALUE(context_report);
  ASSERT_TRUE(context_report->success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest(geometry_path, cooked_root, geometry_descriptor,
      { context_cooked_root }));
  ASSERT_HAS_VALUE(geometry_report);
  EXPECT_FALSE(geometry_report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    geometry_report->diagnostics, "geometry.buffer.sidecar_ambiguous"));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  BufferReferenceOutsideMountedRootsFailsWithHelpfulDiagnostic)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "unmounted_buffer_reference";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "unmounted.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = MakeStandardDescriptorDoc("UnmountedBuffers",
    "/.cooked/Materials/default.omat",
    "/foreign/Resources/Buffers/foreign_vertices.obuf",
    "/foreign/Resources/Buffers/foreign_indices.obuf", "lod0", std::nullopt);
  WriteText(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, "geometry.buffer.virtual_path_unmounted"));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  UnknownBufferViewReferenceFailsWithHelpfulDiagnostic)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "missing_buffer_view_reference";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "missing_view.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  const auto descriptor_doc = MakeStandardDescriptorDoc("CubeMissingView",
    "/.cooked/Materials/default.omat",
    "/.cooked/Resources/Buffers/cube_vertices.obuf",
    "/.cooked/Resources/Buffers/cube_indices.obuf", "lod_missing",
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
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(
    HasDiagnosticCode(report->diagnostics, "geometry.buffer.view_missing"));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  EquivalentLocalBuffersAcrossGeometryJobsWithDifferentVirtualPathsFail)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "cross_job_dedup_virtual_path_conflict";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_a_path = source_dir / "shared_a.geometry.json";
  const auto descriptor_b_path = source_dir / "shared_b.geometry.json";
  const auto vb_source = source_dir / "shared.vertices.buffer.bin";
  const auto ib_source = source_dir / "shared.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x02 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
  };
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  const auto descriptor_a
    = MakeStandardDescriptorDoc("SharedA", "/.cooked/Materials/default.omat",
      "/.cooked/Resources/Buffers/shared_vertices.obuf",
      "/.cooked/Resources/Buffers/shared_indices.obuf", "lod0",
      json::array({
        json {
          { "uri", vb_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/shared_vertices.obuf" },
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
          { "virtual_path", "/.cooked/Resources/Buffers/shared_indices.obuf" },
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
  WriteText(descriptor_a_path, descriptor_a.dump(2));

  const auto descriptor_b
    = MakeStandardDescriptorDoc("SharedB", "/.cooked/Materials/default.omat",
      "/.cooked/Resources/Buffers/alt_vertices.obuf",
      "/.cooked/Resources/Buffers/alt_indices.obuf", "lod0",
      json::array({
        json {
          { "uri", vb_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/alt_vertices.obuf" },
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
          { "virtual_path", "/.cooked/Resources/Buffers/alt_indices.obuf" },
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
  WriteText(descriptor_b_path, descriptor_b.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report_a = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_a_path, cooked_root, descriptor_a));
  ASSERT_HAS_VALUE(report_a);
  ASSERT_TRUE(report_a->success);

  const auto report_b = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_b_path, cooked_root, descriptor_b));
  ASSERT_HAS_VALUE(report_b);
  EXPECT_FALSE(report_b->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report_b->diagnostics, "buffer.container.dedup_virtual_path_conflict"));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  EquivalentLocalBuffersWithDifferentVirtualPathsFailWithConflictDiagnostic)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "dedup_virtual_path_conflict";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "dedup_conflict.geometry.json";
  const auto shared_source = source_dir / "shared.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteText(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto shared_bytes = std::array<std::byte, 12> {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x02 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
  };
  WriteBytes(shared_source, std::span<const std::byte>(shared_bytes));

  const auto descriptor_doc = MakeStandardDescriptorDoc("DedupConflict",
    "/.cooked/Materials/default.omat",
    "/.cooked/Resources/Buffers/conflict_vb.obuf",
    "/.cooked/Resources/Buffers/conflict_ib.obuf", "lod0",
    json::array({
      json {
        { "uri", shared_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/conflict_vb.obuf" },
        { "usage_flags", 3U },
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
        { "uri", shared_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/conflict_ib.obuf" },
        { "usage_flags", 3U },
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
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, "buffer.container.dedup_virtual_path_conflict"));
}

} // namespace oxygen::content::import::test
