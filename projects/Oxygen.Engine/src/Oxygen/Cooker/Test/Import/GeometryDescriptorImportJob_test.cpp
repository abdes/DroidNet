//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_geometry_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto WriteBytesFile(
    const std::filesystem::path& path, std::span<const std::byte> bytes) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
  }

  auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view text) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
    ASSERT_TRUE(out.good());
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

  template <typename T>
  auto ReadStructAt(const std::vector<std::byte>& bytes, const size_t offset)
    -> T
  {
    auto out = T {};
    if (bytes.size() < offset + sizeof(T)) {
      return out;
    }
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return out;
  }

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [code](const ImportDiagnostic& diagnostic) {
        return diagnostic.code == code;
      });
  }

  auto DiagnosticSummary(const std::vector<ImportDiagnostic>& diagnostics)
    -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& diagnostic : diagnostics) {
      out << "[" << diagnostic.code << "] " << diagnostic.message;
      if (!diagnostic.object_path.empty()) {
        out << " (" << diagnostic.object_path << ")";
      }
      out << '\n';
    }
    return out.str();
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

  auto MakeBounds() -> json
  {
    return json {
      { "min", json::array({ -0.5F, -0.5F, -0.5F }) },
      { "max", json::array({ 0.5F, 0.5F, 0.5F }) },
    };
  }

  auto MakeCapsuleDescriptor(
    const json& params, const float height, const float radius) -> json
  {
    const auto bounds = json {
      { "min", json::array({ -radius, -radius, -height * 0.5F }) },
      { "max", json::array({ radius, radius, height * 0.5F }) },
    };
    auto procedural
      = json { { "generator", "Capsule" }, { "mesh_name", "Capsule" } };
    if (!params.is_null()) {
      procedural["params"] = params;
    }
    return json {
      { "name", "Capsule" },
      { "bounds", bounds },
      { "lods",
        json::array({ {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", bounds },
          { "procedural", std::move(procedural) },
          { "submeshes",
            json::array({ {
              { "material_ref", "/.cooked/Materials/default.omat" },
              { "views", json::array({ { { "view_ref", "__all__" } } }) },
            } }) },
        } }) },
    };
  }

  auto MakeGeometryRequest(const std::filesystem::path& source_path,
    const std::filesystem::path& cooked_root, const json& descriptor_doc,
    std::vector<std::filesystem::path> cooked_context_roots = {})
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = source_path.lexically_normal();
    request.cooked_root = cooked_root;
    request.cooked_context_roots = std::move(cooked_context_roots);
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.geometry_descriptor = ImportRequest::GeometryDescriptorPayload {
      .normalized_descriptor_json = descriptor_doc.dump(),
    };
    return request;
  }

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

  auto MakeStandardDescriptorDoc(std::string_view name,
    std::string_view material_ref, std::string_view vb_ref,
    std::string_view ib_ref, std::string_view view_ref,
    std::optional<json> local_buffers) -> json
  {
    auto descriptor_doc = json {
      { "name", name },
      { "bounds", MakeBounds() },
      { "lods",
        json::array({
          json {
            { "name", "LOD0" },
            { "mesh_type", "standard" },
            { "bounds", MakeBounds() },
            { "buffers",
              {
                { "vb_ref", vb_ref },
                { "ib_ref", ib_ref },
              } },
            { "submeshes",
              json::array({
                json {
                  { "material_ref", material_ref },
                  { "views",
                    json::array({ json { { "view_ref", view_ref } } }) },
                },
              }) },
          },
        }) },
    };

    if (local_buffers.has_value()) {
      descriptor_doc["buffers"] = std::move(*local_buffers);
    }
    return descriptor_doc;
  }

  auto FindOutputByExtension(const ImportReport& report,
    std::string_view extension) -> std::optional<std::string>
  {
    const auto it = std::ranges::find_if(
      report.outputs, [extension](const ImportOutputRecord& output) {
        return std::filesystem::path(output.path).extension().string()
          == extension;
      });
    if (it == report.outputs.end()) {
      return std::nullopt;
    }
    return it->path;
  }

  auto CanParseGeometryDescriptor(std::vector<std::byte> bytes) -> bool
  {
    if (bytes.empty()) {
      return false;
    }

    auto stream
      = serio::MemoryStream(std::span<std::byte>(bytes.data(), bytes.size()));
    auto reader = serio::Reader(stream);

    auto context = content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;

    try {
      const auto geometry
        = content::loaders::LoadGeometryAsset(std::move(context));
      return geometry != nullptr;
    } catch (...) {
      return false;
    }
  }

} // namespace

NOLINT_TEST(
  GeometryDescriptorImportJobTest, LocalBuffersEmitGeometryAndBufferArtifacts)
{
  const auto root = MakeTempCookedRoot("local_buffers_emit_artifacts");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "cube.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

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
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

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
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "uri", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/cube_indices.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
      }));
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);
  EXPECT_FALSE(
    HasDiagnosticCode(report.diagnostics, "geometry.material.missing"));

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());
  ASSERT_TRUE(std::filesystem::exists(
    cooked_root / std::filesystem::path(*geometry_relpath)));

  const auto has_output = [&](const std::string_view relpath) {
    return std::ranges::any_of(report.outputs,
      [&](const ImportOutputRecord& output) { return output.path == relpath; });
  };
  EXPECT_TRUE(has_output("Resources/Buffers/cube_vertices.obuf"));
  EXPECT_TRUE(has_output("Resources/Buffers/cube_indices.obuf"));
  EXPECT_TRUE(has_output("Resources/buffers.data"));
  EXPECT_TRUE(has_output("Resources/buffers.table"));

  const auto descriptor_bytes
    = ReadBinaryFile(cooked_root / std::filesystem::path(*geometry_relpath));
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
  EXPECT_NE(
    mesh_desc.info.standard.vertex_buffer, data::pak::core::kNoResourceIndex);
  EXPECT_NE(
    mesh_desc.info.standard.index_buffer, data::pak::core::kNoResourceIndex);
  EXPECT_EQ(mesh_desc.submesh_count, 1U);
  EXPECT_EQ(mesh_desc.mesh_view_count, 1U);

  offset += sizeof(data::pak::geometry::MeshDesc);
  const auto submesh_desc
    = ReadStructAt<data::pak::geometry::SubMeshDesc>(descriptor_bytes, offset);
  EXPECT_EQ(submesh_desc.material_asset_key,
    oxygen::data::AssetKey::FromVirtualPath("/.cooked/Materials/default.omat"));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ResolvesPreCookedBufferSidecarsFromMountedRoot)
{
  const auto root = MakeTempCookedRoot("pre_cooked_buffer_sidecars");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto buffer_manifest_path = source_dir / "shared.buffers.json";
  const auto geometry_path = source_dir / "shared_ref.geometry.json";
  const auto vb_source = source_dir / "shared.vertices.buffer.bin";
  const auto ib_source = source_dir / "shared.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

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
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

  const auto buffer_descriptor = json {
    { "name", "SharedBuffers" },
    { "buffers",
      json::array({
        json {
          { "source", vb_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/shared_vertices.obuf" },
          { "usage_flags", 1U },
          { "element_stride", 32U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "source", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/shared_indices.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(buffer_manifest_path, buffer_descriptor.dump(2));

  const auto geometry_descriptor
    = MakeStandardDescriptorDoc("SharedCube", "/.cooked/Materials/default.omat",
      "/.cooked/Resources/Buffers/shared_vertices.obuf",
      "/.cooked/Resources/Buffers/shared_indices.obuf", "lod0", std::nullopt);
  WriteTextFile(geometry_path, geometry_descriptor.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto buffer_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, cooked_root, buffer_descriptor));
  ASSERT_TRUE(buffer_report.success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest(geometry_path, cooked_root, geometry_descriptor));
  EXPECT_TRUE(geometry_report.success);
  EXPECT_EQ(geometry_report.geometry_written, 1U);
  EXPECT_FALSE(HasDiagnosticCode(
    geometry_report.diagnostics, "geometry.buffer.sidecar_missing"));

  const auto geometry_relpath = FindOutputByExtension(geometry_report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());
  ASSERT_TRUE(std::filesystem::exists(
    cooked_root / std::filesystem::path(*geometry_relpath)));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  MissingMaterialReferenceFailsWithHelpfulDiagnostic)
{
  const auto root = MakeTempCookedRoot("missing_material_reference");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "missing_material.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

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
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
      json {
        { "uri", ib_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_indices.obuf" },
        { "usage_flags", 2U },
        { "element_stride", 4U },
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
    }));
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_FALSE(report.success);
  EXPECT_TRUE(
    HasDiagnosticCode(report.diagnostics, "geometry.material.missing"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  NewMaterialRespectsPriorityBeforeIndexPublication)
{
  const auto root = MakeTempCookedRoot("new_material_priority");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "priority.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

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
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
      json {
        { "uri", ib_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_indices.obuf" },
        { "usage_flags", 2U },
        { "element_stride", 4U },
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
    }));
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto library = root / "Library";
  const auto library_key = data::AssetKey::FromVirtualPath("/Library/New.omat");
  auto writer = LooseCookedWriter(library);
  const auto bytes = std::array { std::byte { 1 } };
  writer.WriteAssetDescriptor(library_key, data::AssetType::kMaterial,
    "/.cooked/Materials/new.omat", "Materials/new.omat", bytes);
  static_cast<void>(writer.Finish());
  WriteTextFile(cooked_root / "Materials/new.omat", "new material");
  for (const auto own_wins : { true, false }) {
    auto request
      = MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc);
    request.cooked_context_roots = own_wins
      ? std::vector<std::filesystem::path> { library, cooked_root }
      : std::vector<std::filesystem::path> { cooked_root, library };
    const auto report = SubmitAndWait(service, std::move(request));
    ASSERT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
    const auto descriptor_bytes
      = ReadBinaryFile(cooked_root / "Geometry/NewMaterialPriority.ogeo");
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
  ResolvesBufferSidecarsFromAdditionalCookedContextRoot)
{
  const auto main_root = MakeTempCookedRoot("resolve_from_context_root_main");
  const auto context_root = MakeTempCookedRoot("resolve_from_context_root_ctx");
  const auto source_dir = main_root / "Sources";
  const auto cooked_root = main_root / ".cooked";
  const auto context_cooked_root = context_root / ".cooked";
  const auto buffer_manifest_path = source_dir / "context_shared.buffers.json";
  const auto geometry_path = source_dir / "context_ref.geometry.json";
  const auto vb_source = source_dir / "context.vertices.buffer.bin";
  const auto ib_source = source_dir / "context.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

  const auto buffer_descriptor = json {
    { "name", "ContextSharedBuffers" },
    { "buffers",
      json::array({
        json {
          { "source", vb_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/context_vertices.obuf" },
          { "usage_flags", 1U },
          { "element_stride", 32U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "source", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/context_indices.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(buffer_manifest_path, buffer_descriptor.dump(2));

  const auto geometry_descriptor = MakeStandardDescriptorDoc(
    "ContextSharedCube", "/.cooked/Materials/default.omat",
    "/.cooked/Resources/Buffers/context_vertices.obuf",
    "/.cooked/Resources/Buffers/context_indices.obuf", "lod0", std::nullopt);
  WriteTextFile(geometry_path, geometry_descriptor.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto buffer_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, context_cooked_root, buffer_descriptor));
  ASSERT_TRUE(buffer_report.success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest(geometry_path, cooked_root, geometry_descriptor,
      { context_cooked_root }));
  EXPECT_TRUE(geometry_report.success);
  EXPECT_EQ(geometry_report.geometry_written, 1U);
  EXPECT_FALSE(HasDiagnosticCode(
    geometry_report.diagnostics, "geometry.buffer.sidecar_missing"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  DuplicateMountedBufferSidecarsFailWithAmbiguousDiagnostic)
{
  const auto main_root = MakeTempCookedRoot("ambiguous_sidecar_main");
  const auto context_root = MakeTempCookedRoot("ambiguous_sidecar_ctx");
  const auto source_dir = main_root / "Sources";
  const auto cooked_root = main_root / ".cooked";
  const auto context_cooked_root = context_root / ".cooked";
  const auto buffer_manifest_path = source_dir / "ambiguous.buffers.json";
  const auto geometry_path = source_dir / "ambiguous.geometry.json";
  const auto vb_source = source_dir / "ambiguous.vertices.buffer.bin";
  const auto ib_source = source_dir / "ambiguous.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

  const auto buffer_descriptor = json {
    { "name", "AmbiguousBuffers" },
    { "buffers",
      json::array({
        json {
          { "source", vb_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/ambiguous_vertices.obuf" },
          { "usage_flags", 1U },
          { "element_stride", 32U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "source", ib_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/ambiguous_indices.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(buffer_manifest_path, buffer_descriptor.dump(2));

  const auto geometry_descriptor = MakeStandardDescriptorDoc("AmbiguousCube",
    "/.cooked/Materials/default.omat",
    "/.cooked/Resources/Buffers/ambiguous_vertices.obuf",
    "/.cooked/Resources/Buffers/ambiguous_indices.obuf", "lod0", std::nullopt);
  WriteTextFile(geometry_path, geometry_descriptor.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto main_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, cooked_root, buffer_descriptor));
  ASSERT_TRUE(main_report.success);
  const auto context_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      buffer_manifest_path, context_cooked_root, buffer_descriptor));
  ASSERT_TRUE(context_report.success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest(geometry_path, cooked_root, geometry_descriptor,
      { context_cooked_root }));
  EXPECT_FALSE(geometry_report.success);
  EXPECT_TRUE(HasDiagnosticCode(
    geometry_report.diagnostics, "geometry.buffer.sidecar_ambiguous"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  BufferReferenceOutsideMountedRootsFailsWithHelpfulDiagnostic)
{
  const auto root = MakeTempCookedRoot("unmounted_buffer_reference");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "unmounted.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = MakeStandardDescriptorDoc("UnmountedBuffers",
    "/.cooked/Materials/default.omat",
    "/foreign/Resources/Buffers/foreign_vertices.obuf",
    "/foreign/Resources/Buffers/foreign_indices.obuf", "lod0", std::nullopt);
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_FALSE(report.success);
  EXPECT_TRUE(HasDiagnosticCode(
    report.diagnostics, "geometry.buffer.virtual_path_unmounted"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  UnknownBufferViewReferenceFailsWithHelpfulDiagnostic)
{
  const auto root = MakeTempCookedRoot("missing_buffer_view_reference");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "missing_view.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto vb_bytes = std::array<std::byte, 96> {};
  const auto ib_bytes = std::array<std::byte, 12> {};
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

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
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
      json {
        { "uri", ib_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/cube_indices.obuf" },
        { "usage_flags", 2U },
        { "element_stride", 4U },
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
    }));
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_FALSE(report.success);
  EXPECT_TRUE(
    HasDiagnosticCode(report.diagnostics, "geometry.buffer.view_missing"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ProceduralDescriptorImportsAndRemainsLoaderCompatible)
{
  const auto root = MakeTempCookedRoot("procedural_loader_compatible");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "cube.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = json {
    { "name", "ProceduralCube" },
    { "bounds", MakeBounds() },
    { "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", MakeBounds() },
          { "procedural",
            {
              { "generator", "Cube" },
              { "mesh_name", "UnitCube" },
            } },
          { "submeshes",
            json::array({
              json {
                { "material_ref", "/.cooked/Materials/default.omat" },
                { "views",
                  json::array({ json { { "view_ref", "__all__" } } }) },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());

  const auto descriptor_bytes
    = ReadBinaryFile(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ProceduralPlanePreservesDescriptorBoundsInCookedMetadata)
{
  const auto root = MakeTempCookedRoot("procedural_plane_bounds_xy_z0");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "floor.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto floor_bounds = json {
    { "min", json::array({ -5.0F, -5.0F, 0.0F }) },
    { "max", json::array({ 5.0F, 5.0F, 0.0F }) },
  };
  const auto descriptor_doc = json {
    { "name", "FloorPlane" },
    { "bounds", floor_bounds },
    { "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", floor_bounds },
          { "procedural",
            {
              { "generator", "Plane" },
              { "mesh_name", "Floor" },
              { "params",
                {
                  { "x_segments", 10U },
                  { "z_segments", 10U },
                  { "size", 10.0F },
                } },
            } },
          { "submeshes",
            json::array({
              json {
                { "material_ref", "/.cooked/Materials/default.omat" },
                { "views",
                  json::array({ json { { "view_ref", "__all__" } } }) },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());

  const auto descriptor_bytes
    = ReadBinaryFile(cooked_root / std::filesystem::path(*geometry_relpath));
  ASSERT_GE(
    descriptor_bytes.size(), sizeof(data::pak::geometry::GeometryAssetDesc));

  const auto geometry_desc
    = ReadStructAt<data::pak::geometry::GeometryAssetDesc>(
      descriptor_bytes, 0U);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_min[0], -5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_min[1], -5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_min[2], 0.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_max[0], 5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_max[1], 5.0F);
  EXPECT_FLOAT_EQ(geometry_desc.bounding_box_max[2], 0.0F);
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ProceduralCapsuleParametersRoundTripThroughNativeLoader)
{
  struct Case {
    std::string_view name;
    json params;
    uint32_t hemisphere_segments;
    uint32_t radial_segments;
    float height;
    float radius;
  };
  const auto cases = std::vector<Case> {
    { "capsule_defaults", nullptr, 8U, 32U, 2.0F, 0.5F },
    { "capsule_custom",
      { { "hemisphere_segments", 4 }, { "radial_segments", 16 },
        { "height", 3.0 }, { "radius", 0.75 } },
      4U, 16U, 3.0F, 0.75F },
    { "capsule_sphere", { { "height", 1.0 } }, 8U, 32U, 1.0F, 0.5F },
  };
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    const auto root = MakeTempCookedRoot(test_case.name);
    const auto cooked_root = root / ".cooked";
    const auto source_path = root / "Sources" / "capsule.geometry.json";
    WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");
    const auto doc = MakeCapsuleDescriptor(
      test_case.params, test_case.height, test_case.radius);
    WriteTextFile(source_path, doc.dump(2));
    const auto report = SubmitAndWait(
      service, MakeGeometryRequest(source_path, cooked_root, doc));
    ASSERT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
    ASSERT_EQ(report.geometry_written, 1U);
    const auto output = FindOutputByExtension(report, ".ogeo");
    ASSERT_TRUE(output.has_value());
    auto bytes = ReadBinaryFile(cooked_root / *output);

    constexpr auto mesh_offset = sizeof(data::pak::geometry::GeometryAssetDesc);
    constexpr auto params_offset
      = mesh_offset + sizeof(data::pak::geometry::MeshDesc);
    ASSERT_GE(bytes.size(), params_offset + 16U);
    const auto mesh_desc
      = ReadStructAt<data::pak::geometry::MeshDesc>(bytes, mesh_offset);
    EXPECT_TRUE(mesh_desc.IsProcedural());
    EXPECT_STREQ(mesh_desc.name, "Capsule/Capsule");
    EXPECT_EQ(mesh_desc.info.procedural.params_size, 16U);
    EXPECT_EQ(ReadStructAt<uint32_t>(bytes, params_offset),
      test_case.hemisphere_segments);
    EXPECT_EQ(ReadStructAt<uint32_t>(bytes, params_offset + 4U),
      test_case.radial_segments);
    EXPECT_FLOAT_EQ(
      ReadStructAt<float>(bytes, params_offset + 8U), test_case.height);
    EXPECT_FLOAT_EQ(
      ReadStructAt<float>(bytes, params_offset + 12U), test_case.radius);

    auto stream = serio::MemoryStream(std::span<std::byte>(bytes));
    auto reader = serio::Reader(stream);
    auto context = content::LoaderContext {};
    context.desc_reader = &reader;
    context.parse_only = true;
    const auto geometry
      = content::loaders::LoadGeometryAsset(std::move(context));
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->LodCount(), 1U);
    const auto& mesh = geometry->MeshAt(0);
    ASSERT_NE(mesh, nullptr);
    ASSERT_GT(mesh->VertexCount(), 0U);
    ASSERT_GT(mesh->IndexCount(), 0U);
    const auto expected
      = data::MakeCapsuleMeshAsset(test_case.hemisphere_segments,
        test_case.radial_segments, test_case.height, test_case.radius);
    ASSERT_TRUE(expected.has_value());
    const auto loaded_vertices = mesh->Vertices();
    ASSERT_EQ(loaded_vertices.size(), expected->first.size());
    for (size_t vertex_index = 0; vertex_index < loaded_vertices.size();
      ++vertex_index) {
      const auto& actual_vertex = loaded_vertices[vertex_index];
      const auto& expected_vertex = expected->first[vertex_index];
      EXPECT_EQ(actual_vertex.position, expected_vertex.position);
      EXPECT_EQ(actual_vertex.normal, expected_vertex.normal);
      EXPECT_EQ(actual_vertex.texcoord, expected_vertex.texcoord);
      EXPECT_EQ(actual_vertex.tangent, expected_vertex.tangent);
      EXPECT_EQ(actual_vertex.bitangent, expected_vertex.bitangent);
      EXPECT_EQ(actual_vertex.color, expected_vertex.color);
    }
    const auto loaded_indices = mesh->IndexBuffer().AsU32();
    ASSERT_EQ(loaded_indices.size(), expected->second.size());
    for (size_t index = 0; index < expected->second.size(); ++index) {
      EXPECT_EQ(loaded_indices[index], expected->second[index]);
    }
    const auto minimum = mesh->BoundingBoxMin();
    const auto maximum = mesh->BoundingBoxMax();
    EXPECT_FLOAT_EQ(minimum.x, -test_case.radius);
    EXPECT_FLOAT_EQ(minimum.y, -test_case.radius);
    EXPECT_FLOAT_EQ(minimum.z, -test_case.height * 0.5F);
    EXPECT_FLOAT_EQ(maximum.x, test_case.radius);
    EXPECT_FLOAT_EQ(maximum.y, test_case.radius);
    EXPECT_FLOAT_EQ(maximum.z, test_case.height * 0.5F);
    ASSERT_EQ(mesh->SubMeshes().size(), 1U);
    const auto views = mesh->SubMeshes().front().MeshViews();
    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views.front().VertexCount(), mesh->VertexCount());
    EXPECT_EQ(views.front().IndexCount(), mesh->IndexCount());
  }
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ProceduralCapsuleRejectsUnrepresentableDimensionsWithoutOutput)
{
  const auto cases = std::vector<json> {
    { { "height", 0.75 }, { "radius", 0.5 } },
    { { "radius", 1.0e-50 } },
    { { "height", 1.0e10 }, { "radius", 0.5 } },
  };
  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });
  for (size_t index = 0; index < cases.size(); ++index) {
    SCOPED_TRACE(cases[index].dump());
    const auto root
      = MakeTempCookedRoot("capsule_invalid_" + std::to_string(index));
    const auto cooked_root = root / ".cooked";
    const auto source_path = root / "Sources" / "capsule.geometry.json";
    WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");
    const auto doc = MakeCapsuleDescriptor(cases[index], 2.0F, 0.5F);
    WriteTextFile(source_path, doc.dump(2));
    const auto report = SubmitAndWait(
      service, MakeGeometryRequest(source_path, cooked_root, doc));
    EXPECT_FALSE(report.success);
    EXPECT_EQ(report.geometry_written, 0U);
    EXPECT_FALSE(FindOutputByExtension(report, ".ogeo").has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "geometry.procedural.generation_failed"))
      << DiagnosticSummary(report.diagnostics);
  }
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ProceduralIcoSphereParametersRoundTripThroughNativeLoader)
{
  const auto root
    = MakeTempCookedRoot("procedural_icosphere_loader_compatible");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path
    = source_dir / "icosphere.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = json {
    { "name", "ProceduralIcoSphere" },
    { "bounds", MakeBounds() },
    { "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", MakeBounds() },
          { "procedural",
            {
              { "generator", "IcoSphere" },
              { "mesh_name", "SoftBall" },
              { "params", { { "subdivision_level", 2 } } },
            } },
          { "submeshes",
            json::array({
              json {
                { "material_ref", "/.cooked/Materials/default.omat" },
                { "views",
                  json::array({ json { { "view_ref", "__all__" } } }) },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());

  auto descriptor_bytes
    = ReadBinaryFile(cooked_root / std::filesystem::path(*geometry_relpath));
  constexpr auto mesh_offset = sizeof(data::pak::geometry::GeometryAssetDesc);
  constexpr auto params_offset
    = mesh_offset + sizeof(data::pak::geometry::MeshDesc);
  ASSERT_GE(descriptor_bytes.size(), params_offset + sizeof(uint32_t));
  const auto mesh_desc = ReadStructAt<data::pak::geometry::MeshDesc>(
    descriptor_bytes, mesh_offset);
  EXPECT_TRUE(mesh_desc.IsProcedural());
  EXPECT_STREQ(mesh_desc.name, "IcoSphere/SoftBall");
  EXPECT_EQ(mesh_desc.info.procedural.params_size, sizeof(uint32_t));
  EXPECT_EQ(ReadStructAt<uint32_t>(descriptor_bytes, params_offset), 2U);

  auto stream = serio::MemoryStream(std::span<std::byte>(descriptor_bytes));
  auto reader = serio::Reader(stream);
  auto context = content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  const auto geometry = content::loaders::LoadGeometryAsset(std::move(context));
  ASSERT_NE(geometry, nullptr);
  ASSERT_EQ(geometry->LodCount(), 1U);
  const auto& mesh = geometry->MeshAt(0);
  ASSERT_NE(mesh, nullptr);
  const auto expected = data::MakeIcoSphereMeshAsset(2);
  ASSERT_TRUE(expected.has_value());
  const auto vertices = mesh->Vertices();
  ASSERT_EQ(vertices.size(), expected->first.size());
  for (size_t index = 0; index < vertices.size(); ++index) {
    EXPECT_EQ(vertices[index].position, expected->first[index].position);
    EXPECT_EQ(vertices[index].normal, expected->first[index].normal);
  }
  const auto indices = mesh->IndexBuffer().AsU32();
  ASSERT_EQ(indices.size(), expected->second.size());
  for (size_t index = 0; index < indices.size(); ++index) {
    EXPECT_EQ(indices[index], expected->second[index]);
  }
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  ProceduralSubdividedCubeImportsAndRemainsLoaderCompatible)
{
  const auto root = MakeTempCookedRoot("procedural_subdivided_cube_loader");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path
    = source_dir / "subdivided_cube.procedural.geometry.json";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  const auto descriptor_doc = json {
    { "name", "ProceduralSubdividedCube" },
    { "bounds", MakeBounds() },
    { "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", MakeBounds() },
          { "procedural",
            {
              { "generator", "SubdividedCube" },
              { "mesh_name", "JellyCube" },
              { "params", { { "segments", 8 } } },
            } },
          { "submeshes",
            json::array({
              json {
                { "material_ref", "/.cooked/Materials/default.omat" },
                { "views",
                  json::array({ json { { "view_ref", "__all__" } } }) },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());

  const auto descriptor_bytes
    = ReadBinaryFile(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  SkinnedDescriptorWithLocalBuffersImportsSuccessfully)
{
  const auto root = MakeTempCookedRoot("skinned_local_buffers_success");
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
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

  auto vb_bytes = std::array<std::byte, 96> {};
  auto ib_bytes = std::array<std::byte, 12> {};
  auto joint_index_bytes = std::array<std::byte, 48> {};
  auto joint_weight_bytes = std::array<std::byte, 48> {};
  auto inverse_bind_bytes = std::array<std::byte, 192> {};
  auto joint_remap_bytes = std::array<std::byte, 12> {};
  for (size_t i = 0; i < vb_bytes.size(); ++i) {
    vb_bytes[i] = static_cast<std::byte>((i + 1U) & 0xFFU);
  }
  for (size_t i = 0; i < ib_bytes.size(); ++i) {
    ib_bytes[i] = static_cast<std::byte>((i * 3U + 7U) & 0xFFU);
  }
  for (size_t i = 0; i < joint_index_bytes.size(); ++i) {
    joint_index_bytes[i] = static_cast<std::byte>((i * 5U + 11U) & 0xFFU);
  }
  for (size_t i = 0; i < joint_weight_bytes.size(); ++i) {
    joint_weight_bytes[i] = static_cast<std::byte>((i * 7U + 13U) & 0xFFU);
  }
  for (size_t i = 0; i < inverse_bind_bytes.size(); ++i) {
    inverse_bind_bytes[i] = static_cast<std::byte>((i * 9U + 17U) & 0xFFU);
  }
  for (size_t i = 0; i < joint_remap_bytes.size(); ++i) {
    joint_remap_bytes[i] = static_cast<std::byte>((i * 11U + 19U) & 0xFFU);
  }
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));
  WriteBytesFile(
    joint_index_source, std::span<const std::byte>(joint_index_bytes));
  WriteBytesFile(
    joint_weight_source, std::span<const std::byte>(joint_weight_bytes));
  WriteBytesFile(
    inverse_bind_source, std::span<const std::byte>(inverse_bind_bytes));
  WriteBytesFile(
    joint_remap_source, std::span<const std::byte>(joint_remap_bytes));

  const auto descriptor_doc = json {
    { "name", "SkinnedCube" },
    { "bounds", MakeBounds() },
    { "buffers",
      json::array({
        json {
          { "uri", vb_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/skinned_vb.obuf" },
          { "usage_flags", 1U },
          { "element_stride", 32U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "uri", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/skinned_ib.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "uri", joint_index_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/skinned_joint_index.obuf" },
          { "usage_flags", 8U },
          { "element_stride", 16U },
        },
        json {
          { "uri", joint_weight_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/skinned_joint_weight.obuf" },
          { "usage_flags", 8U },
          { "element_stride", 16U },
        },
        json {
          { "uri", inverse_bind_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/skinned_inverse_bind.obuf" },
          { "usage_flags", 8U },
          { "element_stride", 64U },
        },
        json {
          { "uri", joint_remap_source.generic_string() },
          { "virtual_path",
            "/.cooked/Resources/Buffers/skinned_joint_remap.obuf" },
          { "usage_flags", 8U },
          { "element_stride", 4U },
        },
      }) },
    { "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "skinned" },
          { "bounds", MakeBounds() },
          { "buffers",
            {
              { "vb_ref", "/.cooked/Resources/Buffers/skinned_vb.obuf" },
              { "ib_ref", "/.cooked/Resources/Buffers/skinned_ib.obuf" },
            } },
          { "skinning",
            {
              { "joint_index_ref",
                "/.cooked/Resources/Buffers/skinned_joint_index.obuf" },
              { "joint_weight_ref",
                "/.cooked/Resources/Buffers/skinned_joint_weight.obuf" },
              { "inverse_bind_ref",
                "/.cooked/Resources/Buffers/skinned_inverse_bind.obuf" },
              { "joint_remap_ref",
                "/.cooked/Resources/Buffers/skinned_joint_remap.obuf" },
              { "joint_count", 3U },
              { "influences_per_vertex", 4U },
            } },
          { "submeshes",
            json::array({
              json {
                { "material_ref", "/.cooked/Materials/default.omat" },
                { "views", json::array({ json { { "view_ref", "lod0" } } }) },
              },
            }) },
        },
      }) },
  };
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_TRUE(report.success) << DiagnosticSummary(report.diagnostics);
  EXPECT_EQ(report.geometry_written, 1U)
    << DiagnosticSummary(report.diagnostics);

  const auto geometry_relpath = FindOutputByExtension(report, ".ogeo");
  ASSERT_TRUE(geometry_relpath.has_value());

  const auto descriptor_bytes
    = ReadBinaryFile(cooked_root / std::filesystem::path(*geometry_relpath));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  EquivalentLocalBuffersAcrossGeometryJobsWithDifferentVirtualPathsFail)
{
  const auto root = MakeTempCookedRoot("cross_job_dedup_virtual_path_conflict");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_a_path = source_dir / "shared_a.geometry.json";
  const auto descriptor_b_path = source_dir / "shared_b.geometry.json";
  const auto vb_source = source_dir / "shared.vertices.buffer.bin";
  const auto ib_source = source_dir / "shared.indices.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

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
  WriteBytesFile(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytesFile(ib_source, std::span<const std::byte>(ib_bytes));

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
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "uri", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/shared_indices.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
      }));
  WriteTextFile(descriptor_a_path, descriptor_a.dump(2));

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
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
        json {
          { "uri", ib_source.generic_string() },
          { "virtual_path", "/.cooked/Resources/Buffers/alt_indices.obuf" },
          { "usage_flags", 2U },
          { "element_stride", 4U },
          { "views",
            json::array({
              json {
                { "name", "lod0" },
                { "element_offset", 0U },
                { "element_count", 3U },
              },
            }) },
        },
      }));
  WriteTextFile(descriptor_b_path, descriptor_b.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report_a = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_a_path, cooked_root, descriptor_a));
  ASSERT_TRUE(report_a.success);

  const auto report_b = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_b_path, cooked_root, descriptor_b));
  EXPECT_FALSE(report_b.success);
  EXPECT_TRUE(HasDiagnosticCode(
    report_b.diagnostics, "buffer.container.dedup_virtual_path_conflict"));
}

NOLINT_TEST(GeometryDescriptorImportJobTest,
  EquivalentLocalBuffersWithDifferentVirtualPathsFailWithConflictDiagnostic)
{
  const auto root = MakeTempCookedRoot("dedup_virtual_path_conflict");
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "dedup_conflict.geometry.json";
  const auto shared_source = source_dir / "shared.buffer.bin";

  std::filesystem::create_directories(cooked_root / "Materials");
  WriteTextFile(cooked_root / "Materials" / "default.omat", "placeholder");

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
  WriteBytesFile(shared_source, std::span<const std::byte>(shared_bytes));

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
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
      json {
        { "uri", shared_source.generic_string() },
        { "virtual_path", "/.cooked/Resources/Buffers/conflict_ib.obuf" },
        { "usage_flags", 3U },
        { "element_stride", 4U },
        { "views",
          json::array({
            json {
              { "name", "lod0" },
              { "element_offset", 0U },
              { "element_count", 3U },
            },
          }) },
      },
    }));
  WriteTextFile(descriptor_path, descriptor_doc.dump(2));

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service]() { service.Stop(); });

  const auto report = SubmitAndWait(
    service, MakeGeometryRequest(descriptor_path, cooked_root, descriptor_doc));
  EXPECT_FALSE(report.success);
  EXPECT_TRUE(HasDiagnosticCode(
    report.diagnostics, "buffer.container.dedup_virtual_path_conflict"));
}

} // namespace oxygen::content::import::test
