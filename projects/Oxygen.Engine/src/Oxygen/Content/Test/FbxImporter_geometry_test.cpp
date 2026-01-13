//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AssetImporter;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::data::AssetType;
using oxygen::data::loose_cooked::v1::FileKind;
using oxygen::data::pak::BufferResourceDesc;
using oxygen::data::pak::GeometryAssetDesc;
using oxygen::data::pak::MaterialAssetDesc;
using oxygen::data::pak::MeshDesc;
using oxygen::data::pak::MeshViewDesc;
using oxygen::data::pak::SubMeshDesc;
using oxygen::data::pak::TextureResourceDesc;
using oxygen::serio::FileStream;
using oxygen::serio::Reader;

class FbxImporterGeometryTest : public oxygen::content::test::FbxImporterTest {
};

[[nodiscard]] auto MakeBmp2x2() -> std::vector<std::byte>
{
  std::vector<std::byte> bytes;
  bytes.reserve(70);

  const auto push_u16 = [&](const uint16_t v) {
    bytes.push_back(std::byte { static_cast<uint8_t>(v & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFu) });
  };
  const auto push_u32 = [&](const uint32_t v) {
    bytes.push_back(std::byte { static_cast<uint8_t>(v & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFu) });
    bytes.push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFu) });
  };
  const auto push_i32
    = [&](const int32_t v) { push_u32(static_cast<uint32_t>(v)); };
  const auto push_bgra
    = [&](const uint8_t b, const uint8_t g, const uint8_t r, const uint8_t a) {
        bytes.push_back(std::byte { b });
        bytes.push_back(std::byte { g });
        bytes.push_back(std::byte { r });
        bytes.push_back(std::byte { a });
      };

  constexpr uint32_t kFileSize = 14u + 40u + 16u;
  constexpr uint32_t kDataOffset = 14u + 40u;

  // BITMAPFILEHEADER
  push_u16(0x4D42u);
  push_u32(kFileSize);
  push_u16(0u);
  push_u16(0u);
  push_u32(kDataOffset);

  // BITMAPINFOHEADER
  push_u32(40u);
  push_i32(2);
  push_i32(2);
  push_u16(1u);
  push_u16(32u);
  push_u32(0u);
  push_u32(16u);
  push_i32(0);
  push_i32(0);
  push_u32(0u);
  push_u32(0u);

  // Pixel data (BGRA), bottom-up rows: blue, white, red, green.
  push_bgra(255u, 0u, 0u, 255u);
  push_bgra(255u, 255u, 255u, 255u);
  push_bgra(0u, 0u, 255u, 255u);
  push_bgra(0u, 255u, 0u, 255u);

  return bytes;
}

[[nodiscard]] auto FindFirstMaterialAsset(
  const LooseCookedInspection& inspection)
  -> std::optional<LooseCookedInspection::AssetEntry>
{
  const auto assets = inspection.Assets();
  const auto it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kMaterial);
    });

  if (it == assets.end()) {
    return std::nullopt;
  }
  return *it;
}

[[nodiscard]] auto ResolveNameWithStrategy(
  const std::shared_ptr<const oxygen::content::import::NamingStrategy>&
    strategy,
  std::string_view authored_name,
  const oxygen::content::import::ImportNameKind kind, const uint32_t ordinal)
  -> std::string
{
  if (strategy) {
    const oxygen::content::import::NamingContext context {
      .kind = kind,
      .ordinal = ordinal,
      .parent_name = {},
      .source_id = {},
    };
    if (const auto renamed = strategy->Rename(authored_name, context);
      renamed.has_value()) {
      return renamed.value();
    }
  }

  return std::string(authored_name);
}

[[nodiscard]] auto MakeDeterministicAssetKey(std::string_view virtual_path)
  -> oxygen::data::AssetKey
{
  const auto bytes = std::as_bytes(
    std::span(virtual_path.data(), static_cast<size_t>(virtual_path.size())));
  const auto digest = oxygen::base::ComputeSha256(bytes);

  oxygen::data::AssetKey key {};
  std::copy_n(digest.begin(), key.guid.size(), key.guid.begin());
  return key;
}

struct LoadedGeometryDesc final {
  GeometryAssetDesc geo_desc {};
  MeshDesc mesh_desc {};
  std::vector<SubMeshDesc> submeshes;
  std::vector<MeshViewDesc> views;
};

[[nodiscard]] auto LoadGeometryDescriptor(const std::filesystem::path& path)
  -> LoadedGeometryDesc
{
  LoadedGeometryDesc loaded;

  FileStream<> stream(path, std::ios::in);
  Reader<FileStream<>> reader(stream);
  auto pack = reader.ScopedAlignment(1);

  auto geo_desc_result = reader.ReadBlobInto(std::as_writable_bytes(
    std::span<GeometryAssetDesc, 1>(&loaded.geo_desc, 1)));
  EXPECT_TRUE(geo_desc_result);
  if (!geo_desc_result) {
    return loaded;
  }

  auto mesh_desc_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<MeshDesc, 1>(&loaded.mesh_desc, 1)));
  EXPECT_TRUE(mesh_desc_result);
  if (!mesh_desc_result) {
    return loaded;
  }

  loaded.submeshes.reserve(loaded.mesh_desc.submesh_count);
  loaded.views.reserve(loaded.mesh_desc.submesh_count);

  for (uint32_t sm_i = 0; sm_i < loaded.mesh_desc.submesh_count; ++sm_i) {
    SubMeshDesc sm_desc {};
    auto sm_desc_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SubMeshDesc, 1>(&sm_desc, 1)));
    EXPECT_TRUE(sm_desc_result);
    if (!sm_desc_result) {
      break;
    }
    loaded.submeshes.push_back(sm_desc);

    for (uint32_t view_i = 0; view_i < sm_desc.mesh_view_count; ++view_i) {
      MeshViewDesc view_desc {};
      auto view_desc_result = reader.ReadBlobInto(
        std::as_writable_bytes(std::span<MeshViewDesc, 1>(&view_desc, 1)));
      EXPECT_TRUE(view_desc_result);
      if (!view_desc_result) {
        break;
      }
      loaded.views.push_back(view_desc);
    }
  }

  return loaded;
}

struct LoadedBuffers final {
  std::vector<BufferResourceDesc> table;
  std::vector<std::byte> data;
};

[[nodiscard]] auto LoadBuffersFromCooked(
  const std::filesystem::path& cooked_root,
  const LooseCookedInspection& inspection) -> LoadedBuffers
{
  LoadedBuffers buffers;

  const auto files = inspection.Files();
  const auto table_it = std::find_if(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kBuffersTable; });
  const auto data_it = std::find_if(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kBuffersData; });

  EXPECT_NE(table_it, files.end());
  EXPECT_NE(data_it, files.end());
  if (table_it == files.end() || data_it == files.end()) {
    return buffers;
  }

  const auto table_path
    = cooked_root / std::filesystem::path(table_it->relpath);
  const auto data_path = cooked_root / std::filesystem::path(data_it->relpath);

  const auto table_size = std::filesystem::file_size(table_path);
  EXPECT_EQ(table_size % sizeof(BufferResourceDesc), 0u);
  const auto count
    = static_cast<size_t>(table_size / sizeof(BufferResourceDesc));
  buffers.table.resize(count);

  {
    FileStream<> stream(table_path, std::ios::in);
    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto read_result
      = reader.ReadBlobInto(std::as_writable_bytes(std::span(buffers.table)));
    EXPECT_TRUE(read_result);
  }

  {
    const auto data_size = std::filesystem::file_size(data_path);
    buffers.data.resize(static_cast<size_t>(data_size));
    FileStream<> stream(data_path, std::ios::in);
    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto read_result
      = reader.ReadBlobInto(std::as_writable_bytes(std::span(buffers.data)));
    EXPECT_TRUE(read_result);
  }

  return buffers;
}

[[nodiscard]] auto FindFirstGeometryAsset(
  const LooseCookedInspection& inspection)
  -> std::optional<LooseCookedInspection::AssetEntry>
{
  const auto assets = inspection.Assets();
  const auto geo_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kGeometry);
    });

  if (geo_it == assets.end()) {
    return std::nullopt;
  }
  return *geo_it;
}

//! Test: Real FBX backend emits a GeometryAsset with valid mesh structure.
/*!\
 Scenario: Writes a minimal ASCII FBX containing a single triangulated mesh.
 Runs the default AssetImporter() (real FbxImporter backend), requesting
 geometry-only output.

 Verifies:
 - a geometry descriptor is emitted and indexed,
 - buffers.table + buffers.data are emitted as a required pair,
 - the geometry descriptor contains exactly one LOD with one mesh, one
   submesh, and one mesh view.
*/
NOLINT_TEST_F(
  FbxImporterGeometryTest, RealBackend_EmitsGeometry_WithValidMeshStructure)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_geometry");
  const auto source_path = temp_dir / "triangle.fbx";

  // Minimal FBX ASCII with one mesh and one triangle.
  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 2\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
                          "    Vertices: *9 {\n"
                          "      a: 0,0,0,  1,0,0,  0,1,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *3 {\n"
                          "      a: 0,1,-3\n"
                          "    }\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kGeometry;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.materials_written, 0u);
  EXPECT_EQ(report.geometry_written, 1u);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto files = inspection.Files();
  const auto has_buffers_table = std::any_of(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kBuffersTable; });
  const auto has_buffers_data = std::any_of(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kBuffersData; });
  EXPECT_TRUE(has_buffers_table);
  EXPECT_TRUE(has_buffers_data);

  const auto assets = inspection.Assets();
  const auto geo_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kGeometry);
    });
  ASSERT_NE(geo_it, assets.end());
  EXPECT_GE(geo_it->descriptor_size,
    sizeof(GeometryAssetDesc) + sizeof(MeshDesc) + sizeof(SubMeshDesc)
      + sizeof(MeshViewDesc));

  const auto desc_path
    = report.cooked_root / std::filesystem::path(geo_it->descriptor_relpath);

  FileStream<> stream(desc_path, std::ios::in);
  Reader<FileStream<>> desc_reader(stream);

  // Validate binary structure (packed reads).
  {
    auto pack = desc_reader.ScopedAlignment(1);

    GeometryAssetDesc geo_desc {};
    auto geo_desc_result = desc_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<GeometryAssetDesc, 1>(&geo_desc, 1)));
    ASSERT_TRUE(geo_desc_result);
    EXPECT_EQ(geo_desc.lod_count, 1u);

    MeshDesc mesh_desc {};
    auto mesh_desc_result = desc_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MeshDesc, 1>(&mesh_desc, 1)));
    ASSERT_TRUE(mesh_desc_result);
    EXPECT_EQ(mesh_desc.submesh_count, 1u);
    EXPECT_EQ(mesh_desc.mesh_view_count, 1u);

    SubMeshDesc sm_desc {};
    auto sm_desc_result = desc_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SubMeshDesc, 1>(&sm_desc, 1)));
    ASSERT_TRUE(sm_desc_result);
    EXPECT_EQ(sm_desc.mesh_view_count, 1u);

    MeshViewDesc view_desc {};
    auto view_desc_result = desc_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MeshViewDesc, 1>(&view_desc, 1)));
    ASSERT_TRUE(view_desc_result);
    EXPECT_EQ(view_desc.first_index, 0u);
    EXPECT_EQ(view_desc.first_vertex, 0u);
    EXPECT_GT(view_desc.index_count, 0u);
    EXPECT_GT(view_desc.vertex_count, 0u);
  }

  // Re-open and parse through the runtime loader in parse-only mode.
  FileStream<> stream2(desc_path, std::ios::in);
  Reader<FileStream<>> reader2(stream2);

  oxygen::content::LoaderContext context {
    .current_asset_key = geo_it->key,
    .desc_reader = &reader2,
    .work_offline = true,
    .parse_only = true,
  };

  const auto geo_asset = oxygen::content::loaders::LoadGeometryAsset(context);
  EXPECT_NE(geo_asset, nullptr);
}

//! Test: Textures are emitted as resources and wired into materials.
/*!
 Scenario: Writes a minimal ASCII FBX containing one material and one file
 texture connected to the material's DiffuseColor.

 Runs the default AssetImporter() (real FbxImporter backend), requesting
 materials + textures.

 Verifies:
 - textures.table + textures.data are emitted as a required pair,
 - textures.table contains at least the required fallback entry (index 0),
 - the material descriptor references a non-zero base_color_texture index and
   does not set kMaterialFlag_NoTextureSampling.
*/
NOLINT_TEST_F(
  FbxImporterGeometryTest, RealBackend_EmitsTextureResources_AndWiresMaterials)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_textures");
  const auto source_path = temp_dir / "textured_material.fbx";
  const auto texture_path = temp_dir / "diffuse.bmp";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 3\n"
      "  ObjectType: \"Material\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Texture\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Video\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Material: 10, \"Material::Mat\", \"\" {\n"
      "    Version: 102\n"
      "    ShadingModel: \"phong\"\n"
      "    Properties70:  {\n"
      "      P: \"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.8,0.8\n"
      "    }\n"
      "  }\n"
      "  Video: 30, \"Video::Diffuse\", \"Clip\" {\n"
      "    Type: \"Clip\"\n"
      "    FileName: \"diffuse.bmp\"\n"
      "    RelativeFilename: \"diffuse.bmp\"\n"
      "  }\n"
      "  Texture: 20, \"Texture::Diffuse\", \"TextureVideoClip\" {\n"
      "    Type: \"TextureVideoClip\"\n"
      "    Version: 202\n"
      "    TextureName: \"Texture::Diffuse\"\n"
      "    FileName: \"diffuse.bmp\"\n"
      "    RelativeFilename: \"diffuse.bmp\"\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OP\", 20, 10, \"DiffuseColor\"\n"
      "  C: \"OP\", 30, 20, \"Video\"\n"
      "}\n";

  const auto bmp = MakeBmp2x2();
  WriteBinaryFile(
    texture_path, std::span<const std::byte>(bmp.data(), bmp.size()));
  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kMaterials
    | oxygen::content::import::ImportContentFlags::kTextures;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.materials_written, 1u);

  const auto has_decode_warning
    = std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
      [](const auto& d) { return d.code == "fbx.texture_decode_failed"; });
  if (has_decode_warning) {
    for (const auto& d : report.diagnostics) {
      if (d.code == "fbx.texture_decode_failed") {
        ADD_FAILURE() << d.message;
      }
    }
  }

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto files = inspection.Files();
  const auto has_textures_table = std::any_of(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kTexturesTable; });
  const auto has_textures_data = std::any_of(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kTexturesData; });
  EXPECT_TRUE(has_textures_table);
  EXPECT_TRUE(has_textures_data);

  const auto table_it = std::find_if(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kTexturesTable; });
  ASSERT_NE(table_it, files.end());

  const auto table_path
    = report.cooked_root / std::filesystem::path(table_it->relpath);
  const auto table_size = std::filesystem::file_size(table_path);
  EXPECT_EQ(table_size % sizeof(TextureResourceDesc), 0u);
  const auto texture_count
    = static_cast<size_t>(table_size / sizeof(TextureResourceDesc));
  EXPECT_GE(texture_count, 2u);

  // Validate decoded texture metadata.
  std::vector<TextureResourceDesc> table(texture_count);
  {
    FileStream<> stream(table_path, std::ios::in);
    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto read_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span(table.data(), table.size())));
    ASSERT_TRUE(read_result);
  }

  EXPECT_EQ(table[0].width, 1u);
  EXPECT_EQ(table[0].height, 1u);
  // v4 payload includes header + layout table aligned before data
  EXPECT_EQ(table[0].size_bytes, 768u);

  EXPECT_EQ(table[1].width, 2u);
  EXPECT_EQ(table[1].height, 2u);
  // v4 payload includes header + layout table aligned to 512 before data
  EXPECT_EQ(table[1].size_bytes, 1024u);
  EXPECT_EQ(table[1].alignment, 256u);
  EXPECT_EQ(table[1].data_offset % 256u, 0u);

  const auto material_entry_opt = FindFirstMaterialAsset(inspection);
  ASSERT_TRUE(material_entry_opt.has_value());

  const auto mat_path = report.cooked_root
    / std::filesystem::path(material_entry_opt->descriptor_relpath);

  MaterialAssetDesc mat_desc {};
  {
    FileStream<> stream(mat_path, std::ios::in);
    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto read_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MaterialAssetDesc, 1>(&mat_desc, 1)));
    ASSERT_TRUE(read_result);
  }

  EXPECT_NE(mat_desc.base_color_texture, 0u);
  EXPECT_EQ(
    mat_desc.flags & oxygen::data::pak::kMaterialFlag_NoTextureSampling, 0u);

  EXPECT_EQ(
    mat_desc.header.asset_type, static_cast<uint8_t>(AssetType::kMaterial));

  // DiffuseColor should map to scalar base_color fallback.
  EXPECT_NEAR(mat_desc.base_color[0], 0.8F, 1e-4F);
  EXPECT_NEAR(mat_desc.base_color[1], 0.8F, 1e-4F);
  EXPECT_NEAR(mat_desc.base_color[2], 0.8F, 1e-4F);
  EXPECT_NEAR(mat_desc.base_color[3], 1.0F, 1e-4F);
}

//! Test: Multiple imports append texture tables.
/*!
 Scenario: Import two different FBX files into the same cooked root.

 Verifies:
 - the second import preserves the first import's textures in textures.table,
 - textures.table grows (fallback + 2 distinct textures),
 - both materials reference valid, distinct texture indices.
*/
NOLINT_TEST_F(FbxImporterGeometryTest, RealBackend_MultiImport_AppendsTextures)
{
  using oxygen::data::pak::MaterialAssetDesc;

  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_multi_import_textures");
  const auto cooked_root = temp_dir / "cooked";

  const auto source_a = temp_dir / "a.fbx";
  const auto source_b = temp_dir / "b.fbx";
  const auto tex_a = temp_dir / "a.bmp";
  const auto tex_b = temp_dir / "b.bmp";

  const char* kFbxA
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 3\n"
      "  ObjectType: \"Material\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Texture\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Video\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Material: 10, \"Material::MatA\", \"\" {\n"
      "    Version: 102\n"
      "    ShadingModel: \"phong\"\n"
      "    Properties70:  {\n"
      "      P: \"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.8,0.8\n"
      "    }\n"
      "  }\n"
      "  Video: 30, \"Video::A\", \"Clip\" {\n"
      "    Type: \"Clip\"\n"
      "    FileName: \"a.bmp\"\n"
      "    RelativeFilename: \"a.bmp\"\n"
      "  }\n"
      "  Texture: 20, \"Texture::A\", \"TextureVideoClip\" {\n"
      "    Type: \"TextureVideoClip\"\n"
      "    Version: 202\n"
      "    TextureName: \"Texture::A\"\n"
      "    FileName: \"a.bmp\"\n"
      "    RelativeFilename: \"a.bmp\"\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OP\", 20, 10, \"DiffuseColor\"\n"
      "  C: \"OP\", 30, 20, \"Video\"\n"
      "}\n";

  const char* kFbxB
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 3\n"
      "  ObjectType: \"Material\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Texture\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Video\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Material: 10, \"Material::MatB\", \"\" {\n"
      "    Version: 102\n"
      "    ShadingModel: \"phong\"\n"
      "    Properties70:  {\n"
      "      P: \"DiffuseColor\", \"Color\", \"\", \"A\",0.4,0.4,0.4\n"
      "    }\n"
      "  }\n"
      "  Video: 30, \"Video::B\", \"Clip\" {\n"
      "    Type: \"Clip\"\n"
      "    FileName: \"b.bmp\"\n"
      "    RelativeFilename: \"b.bmp\"\n"
      "  }\n"
      "  Texture: 20, \"Texture::B\", \"TextureVideoClip\" {\n"
      "    Type: \"TextureVideoClip\"\n"
      "    Version: 202\n"
      "    TextureName: \"Texture::B\"\n"
      "    FileName: \"b.bmp\"\n"
      "    RelativeFilename: \"b.bmp\"\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OP\", 20, 10, \"DiffuseColor\"\n"
      "  C: \"OP\", 30, 20, \"Video\"\n"
      "}\n";

  const auto bmp_a = MakeBmp2x2();
  auto bmp_b = MakeBmp2x2();
  // Make the second bitmap different so multi-import produces two distinct
  // texture resources.
  ASSERT_FALSE(bmp_b.empty());
  bmp_b.back() ^= std::byte { 0x01 };
  WriteBinaryFile(
    tex_a, std::span<const std::byte>(bmp_a.data(), bmp_a.size()));
  WriteBinaryFile(
    tex_b, std::span<const std::byte>(bmp_b.data(), bmp_b.size()));
  WriteTextFile(source_a, kFbxA);
  WriteTextFile(source_b, kFbxB);

  AssetImporter importer;
  ImportRequest request_a {
    .source_path = source_a,
    .cooked_root = cooked_root,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request_a.options.naming_strategy
    = std::make_shared<NormalizeNamingStrategy>();
  request_a.options.import_content
    = oxygen::content::import::ImportContentFlags::kMaterials
    | oxygen::content::import::ImportContentFlags::kTextures;

  ImportRequest request_b = request_a;
  request_b.source_path = source_b;

  // Act
  const auto report_a = importer.ImportToLooseCooked(request_a);
  const auto report_b = importer.ImportToLooseCooked(request_b);

  // Assert
  EXPECT_TRUE(report_a.success);
  EXPECT_TRUE(report_b.success);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report_b.cooked_root);

  const auto files = inspection.Files();
  const auto table_it = std::find_if(files.begin(), files.end(),
    [](const auto& e) { return e.kind == FileKind::kTexturesTable; });
  ASSERT_NE(table_it, files.end());

  const auto table_path
    = report_b.cooked_root / std::filesystem::path(table_it->relpath);
  const auto table_size = std::filesystem::file_size(table_path);
  ASSERT_EQ(table_size % sizeof(TextureResourceDesc), 0u);
  const auto texture_count
    = static_cast<size_t>(table_size / sizeof(TextureResourceDesc));
  EXPECT_GE(texture_count, 3u);

  std::optional<std::filesystem::path> mat_a_path;
  std::optional<std::filesystem::path> mat_b_path;
  for (const auto& a : inspection.Assets()) {
    if (a.asset_type != static_cast<uint8_t>(AssetType::kMaterial)) {
      continue;
    }
    if (a.virtual_path.find("MatA") != std::string::npos) {
      mat_a_path
        = report_b.cooked_root / std::filesystem::path(a.descriptor_relpath);
    }
    if (a.virtual_path.find("MatB") != std::string::npos) {
      mat_b_path
        = report_b.cooked_root / std::filesystem::path(a.descriptor_relpath);
    }
  }

  ASSERT_TRUE(mat_a_path.has_value());
  ASSERT_TRUE(mat_b_path.has_value());

  auto read_material = [](const std::filesystem::path& p) -> MaterialAssetDesc {
    MaterialAssetDesc d {};
    FileStream<> stream(p, std::ios::in);
    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto read_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MaterialAssetDesc, 1>(&d, 1)));
    EXPECT_TRUE(read_result);
    return d;
  };

  const auto mat_a = read_material(*mat_a_path);
  const auto mat_b = read_material(*mat_b_path);

  EXPECT_LT(mat_a.base_color_texture, texture_count);
  EXPECT_LT(mat_b.base_color_texture, texture_count);
  EXPECT_NE(mat_a.base_color_texture, 0u);
  EXPECT_NE(mat_b.base_color_texture, 0u);
  EXPECT_NE(mat_a.base_color_texture, mat_b.base_color_texture);
}

//! Test: Reimport does not grow texture tables.
/*!
 Scenario: Import the same FBX file twice into the same cooked root.

 Verifies:
 - textures.table size remains stable after the second import,
 - the material's texture index remains stable.
*/
NOLINT_TEST_F(FbxImporterGeometryTest, RealBackend_Reimport_DedupsTextures)
{
  using oxygen::data::pak::MaterialAssetDesc;

  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_reimport_textures");
  const auto cooked_root = temp_dir / "cooked";
  const auto source_path = temp_dir / "scene.fbx";
  const auto texture_path = temp_dir / "diffuse.bmp";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 3\n"
      "  ObjectType: \"Material\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Texture\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Video\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Material: 10, \"Material::Mat\", \"\" {\n"
      "    Version: 102\n"
      "    ShadingModel: \"phong\"\n"
      "    Properties70:  {\n"
      "      P: \"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.8,0.8\n"
      "    }\n"
      "  }\n"
      "  Video: 30, \"Video::Diffuse\", \"Clip\" {\n"
      "    Type: \"Clip\"\n"
      "    FileName: \"diffuse.bmp\"\n"
      "    RelativeFilename: \"diffuse.bmp\"\n"
      "  }\n"
      "  Texture: 20, \"Texture::Diffuse\", \"TextureVideoClip\" {\n"
      "    Type: \"TextureVideoClip\"\n"
      "    Version: 202\n"
      "    TextureName: \"Texture::Diffuse\"\n"
      "    FileName: \"diffuse.bmp\"\n"
      "    RelativeFilename: \"diffuse.bmp\"\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OP\", 20, 10, \"DiffuseColor\"\n"
      "  C: \"OP\", 30, 20, \"Video\"\n"
      "}\n";

  const auto bmp = MakeBmp2x2();
  WriteBinaryFile(
    texture_path, std::span<const std::byte>(bmp.data(), bmp.size()));
  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = cooked_root,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kMaterials
    | oxygen::content::import::ImportContentFlags::kTextures;

  // Act
  const auto report_a = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report_a.success);

  auto get_textures_table_size
    = [](const std::filesystem::path& cooked_root_path) -> uint64_t {
    LooseCookedInspection inspection;
    inspection.LoadFromRoot(cooked_root_path);
    const auto files = inspection.Files();
    const auto table_it = std::find_if(files.begin(), files.end(),
      [](const auto& e) { return e.kind == FileKind::kTexturesTable; });
    EXPECT_NE(table_it, files.end());
    if (table_it == files.end()) {
      return 0;
    }
    const auto table_path
      = cooked_root_path / std::filesystem::path(table_it->relpath);
    return std::filesystem::file_size(table_path);
  };

  auto get_first_material_base_color_texture
    = [](const std::filesystem::path& cooked_root_path) -> uint32_t {
    LooseCookedInspection inspection;
    inspection.LoadFromRoot(cooked_root_path);
    const auto material_entry_opt = FindFirstMaterialAsset(inspection);
    EXPECT_TRUE(material_entry_opt.has_value());
    if (!material_entry_opt.has_value()) {
      return 0;
    }

    const auto mat_path = cooked_root_path
      / std::filesystem::path(material_entry_opt->descriptor_relpath);

    MaterialAssetDesc mat_desc {};
    FileStream<> stream(mat_path, std::ios::in);
    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto read_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MaterialAssetDesc, 1>(&mat_desc, 1)));
    EXPECT_TRUE(read_result);
    return mat_desc.base_color_texture;
  };

  const auto table_a_size = get_textures_table_size(report_a.cooked_root);
  const auto tex_index_a
    = get_first_material_base_color_texture(report_a.cooked_root);

  const auto report_b = importer.ImportToLooseCooked(request);
  EXPECT_TRUE(report_b.success);

  const auto table_b_size = get_textures_table_size(report_b.cooked_root);
  EXPECT_EQ(table_a_size, table_b_size);

  const auto tex_index_b
    = get_first_material_base_color_texture(report_b.cooked_root);

  EXPECT_NE(tex_index_a, 0u);
  EXPECT_NE(tex_index_b, 0u);
  EXPECT_EQ(tex_index_a, tex_index_b);
}

//! Test: Reimport does not grow buffer tables.
/*!
 Scenario: Import the same FBX file twice into the same cooked root.

 Verifies:
 - buffers.table size remains stable after the second import,
 - the container remains loadable through LooseCookedInspection.
*/
NOLINT_TEST_F(FbxImporterGeometryTest, RealBackend_Reimport_DedupsBuffers)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_reimport_buffers");
  const auto cooked_root = temp_dir / "cooked";
  const auto source_path = temp_dir / "tri.fbx";

  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 2\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Tri\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"TriGeo\", \"Mesh\" {\n"
                          "    Vertices: *9 {\n"
                          "      a: 0,0,0,  0,1,0,  1,0,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *3 {\n"
                          "      a: 0,1,-3\n"
                          "    }\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = cooked_root,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kGeometry;

  // Act
  const auto report_a = importer.ImportToLooseCooked(request);
  const auto report_b = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report_a.success);
  EXPECT_TRUE(report_b.success);

  LooseCookedInspection inspection_a;
  inspection_a.LoadFromRoot(report_a.cooked_root);
  const auto files_a = inspection_a.Files();
  const auto table_a_it = std::find_if(files_a.begin(), files_a.end(),
    [](const auto& e) { return e.kind == FileKind::kBuffersTable; });
  ASSERT_NE(table_a_it, files_a.end());
  const auto table_a_path
    = report_a.cooked_root / std::filesystem::path(table_a_it->relpath);
  const auto table_a_size = std::filesystem::file_size(table_a_path);

  LooseCookedInspection inspection_b;
  inspection_b.LoadFromRoot(report_b.cooked_root);
  const auto files_b = inspection_b.Files();
  const auto table_b_it = std::find_if(files_b.begin(), files_b.end(),
    [](const auto& e) { return e.kind == FileKind::kBuffersTable; });
  ASSERT_NE(table_b_it, files_b.end());
  const auto table_b_path
    = report_b.cooked_root / std::filesystem::path(table_b_it->relpath);
  const auto table_b_size = std::filesystem::file_size(table_b_path);

  EXPECT_EQ(table_a_size, table_b_size);
}

//=== UVs + Tangents ===-----------------------------------------------------//

//! Test: Real FBX backend imports UVs and generates tangents when missing.
/*!
 Scenario: A minimal ASCII FBX contains a single triangle with per-polygon-
 vertex UVs (LayerElementUV). No tangents are authored.

 Verifies:
 - the emitted vertices contain the authored UVs,
 - the emitted vertices contain a generated tangent basis consistent with the
   UV mapping (required for normal mapping).
*/
NOLINT_TEST_F(
  FbxImporterGeometryTest, RealBackend_ImportsUvs_AndGeneratesTangents)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_geometry_uv_tangents");
  const auto source_path = temp_dir / "tri_uvs.fbx";

  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 2\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Tri\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"TriGeo\", \"Mesh\" {\n"
                          "    Vertices: *9 {\n"
                          "      a: 0,0,0,  0,1,0,  1,0,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *3 {\n"
                          "      a: 0,1,-3\n"
                          "    }\n"
                          "    LayerElementUV: 0 {\n"
                          "      Version: 101\n"
                          "      Name: \"\"\n"
                          "      MappingInformationType: \"ByPolygonVertex\"\n"
                          "      ReferenceInformationType: \"IndexToDirect\"\n"
                          "      UV: *6 {\n"
                          "        a: 0,0,  1,0,  0,1\n"
                          "      }\n"
                          "      UVIndex: *3 {\n"
                          "        a: 0,1,2\n"
                          "      }\n"
                          "    }\n"
                          "    Layer: 0 {\n"
                          "      Version: 100\n"
                          "      LayerElement:  {\n"
                          "        Type: \"LayerElementUV\"\n"
                          "        TypedIndex: 0\n"
                          "      }\n"
                          "    }\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kGeometry;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  ASSERT_TRUE(report.success);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto geo_asset_opt = FindFirstGeometryAsset(inspection);
  ASSERT_TRUE(geo_asset_opt.has_value());
  const auto& geo_asset = *geo_asset_opt;

  const auto desc_path
    = report.cooked_root / std::filesystem::path(geo_asset.descriptor_relpath);
  const auto loaded = LoadGeometryDescriptor(desc_path);

  const auto buffers = LoadBuffersFromCooked(report.cooked_root, inspection);
  ASSERT_GT(buffers.table.size(), loaded.mesh_desc.info.standard.vertex_buffer);

  const auto vb_desc
    = buffers.table[loaded.mesh_desc.info.standard.vertex_buffer];
  ASSERT_GT(vb_desc.size_bytes, 0u);
  ASSERT_EQ(vb_desc.element_stride, sizeof(oxygen::data::Vertex));

  const auto vb_begin = static_cast<size_t>(vb_desc.data_offset);
  const auto vb_end = vb_begin + static_cast<size_t>(vb_desc.size_bytes);
  ASSERT_LE(vb_end, buffers.data.size());

  const auto* vptr = reinterpret_cast<const oxygen::data::Vertex*>(
    buffers.data.data() + vb_begin);
  const auto vertex_count = vb_desc.size_bytes / sizeof(oxygen::data::Vertex);
  ASSERT_GE(vertex_count, 3u);

  // UVs should be imported (not all zeros).
  EXPECT_GT(std::abs(vptr[1].texcoord.x - vptr[0].texcoord.x)
      + std::abs(vptr[2].texcoord.y - vptr[0].texcoord.y),
    0.5F);

  // Tangent should be consistent with UV mapping (compare against the same
  // triangle tangent computation).
  const auto p0 = vptr[0].position;
  const auto p1 = vptr[1].position;
  const auto p2 = vptr[2].position;
  const auto w0 = vptr[0].texcoord;
  const auto w1 = vptr[1].texcoord;
  const auto w2 = vptr[2].texcoord;

  const auto e1 = p1 - p0;
  const auto e2 = p2 - p0;
  const auto d1 = w1 - w0;
  const auto d2 = w2 - w0;
  const float denom = d1.x * d2.y - d1.y * d2.x;
  ASSERT_GT(std::abs(denom), 1e-8F);
  const float r = 1.0F / denom;
  const auto expected_t = glm::normalize((e1 * d2.y - e2 * d1.y) * r);

  const auto t0 = glm::normalize(vptr[0].tangent);
  EXPECT_GT(glm::dot(t0, expected_t), 0.95F);
}

//=== Multi-material + Vertex Colors ===------------------------------------//

//! Test: Real FBX backend splits faces into per-material submeshes.
/*!
 Scenario: A minimal ASCII FBX contains two materials connected to a single
 mesh node, and assigns one triangle per material using LayerElementMaterial
 (ByPolygon).

 Verifies:
 - the emitted mesh has 2 submeshes and 2 mesh views,
 - mesh view index ranges are contiguous and non-overlapping,
 - each submesh references the correct deterministic material AssetKey.
*/
NOLINT_TEST_F(
  FbxImporterGeometryTest, RealBackend_SplitsMultiMaterialMeshIntoSubmeshes)
{
  // Arrange
  const auto temp_dir
    = MakeTempDir("fbx_importer_real_geometry_multi_material");
  const auto source_path = temp_dir / "quad_two_materials.fbx";

  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 3\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Material\" {\n"
                          "    Count: 2\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Quad\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"QuadGeo\", \"Mesh\" {\n"
                          "    Vertices: *12 {\n"
                          "      a: 0,0,0,  1,0,0,  1,1,0,  0,1,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *6 {\n"
                          "      a: 0,1,-3,  0,2,-4\n"
                          "    }\n"
                          "    LayerElementMaterial: 0 {\n"
                          "      Version: 101\n"
                          "      Name: \"\"\n"
                          "      MappingInformationType: \"ByPolygon\"\n"
                          "      ReferenceInformationType: \"IndexToDirect\"\n"
                          "      Materials: *2 {\n"
                          "        a: 0,1\n"
                          "      }\n"
                          "    }\n"
                          "    Layer: 0 {\n"
                          "      Version: 100\n"
                          "      LayerElement:  {\n"
                          "        Type: \"LayerElementMaterial\"\n"
                          "        TypedIndex: 0\n"
                          "      }\n"
                          "    }\n"
                          "  }\n"
                          "  Material: 3, \"MatA\", \"\" {\n"
                          "    Version: 102\n"
                          "    ShadingModel: \"phong\"\n"
                          "    MultiLayer: 0\n"
                          "  }\n"
                          "  Material: 4, \"MatB\", \"\" {\n"
                          "    Version: 102\n"
                          "    ShadingModel: \"phong\"\n"
                          "    MultiLayer: 0\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "  C: \"OO\", 3, 1\n"
                          "  C: \"OO\", 4, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kGeometry;
  request.options.asset_key_policy
    = oxygen::content::import::AssetKeyPolicy::kDeterministicFromVirtualPath;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  ASSERT_TRUE(report.success);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto geo_asset_opt = FindFirstGeometryAsset(inspection);
  ASSERT_TRUE(geo_asset_opt.has_value());
  const auto& geo_asset = *geo_asset_opt;

  const auto desc_path
    = report.cooked_root / std::filesystem::path(geo_asset.descriptor_relpath);
  const auto loaded = LoadGeometryDescriptor(desc_path);

  EXPECT_EQ(loaded.geo_desc.lod_count, 1u);
  EXPECT_EQ(loaded.mesh_desc.submesh_count, 2u);
  EXPECT_EQ(loaded.mesh_desc.mesh_view_count, 2u);
  ASSERT_EQ(loaded.submeshes.size(), 2u);
  ASSERT_EQ(loaded.views.size(), 2u);

  EXPECT_EQ(loaded.submeshes[0].mesh_view_count, 1u);
  EXPECT_EQ(loaded.submeshes[1].mesh_view_count, 1u);

  EXPECT_EQ(loaded.views[0].first_index, 0u);
  EXPECT_EQ(loaded.views[0].index_count, 3u);
  EXPECT_EQ(loaded.views[1].first_index, 3u);
  EXPECT_EQ(loaded.views[1].index_count, 3u);

  const auto mat_a_name
    = ResolveNameWithStrategy(request.options.naming_strategy, "MatA",
      oxygen::content::import::ImportNameKind::kMaterial, 0);
  const auto mat_b_name
    = ResolveNameWithStrategy(request.options.naming_strategy, "MatB",
      oxygen::content::import::ImportNameKind::kMaterial, 1);

  const auto scene_ns = request.source_path.stem().string();
  const auto vp_a = request.loose_cooked_layout.MaterialVirtualPath(
    scene_ns + "/" + mat_a_name);
  const auto vp_b = request.loose_cooked_layout.MaterialVirtualPath(
    scene_ns + "/" + mat_b_name);
  const auto key_a = MakeDeterministicAssetKey(vp_a);
  const auto key_b = MakeDeterministicAssetKey(vp_b);

  const auto sm0_key = loaded.submeshes[0].material_asset_key;
  const auto sm1_key = loaded.submeshes[1].material_asset_key;

  EXPECT_NE(sm0_key, sm1_key);
  EXPECT_TRUE((sm0_key == key_a && sm1_key == key_b)
    || (sm0_key == key_b && sm1_key == key_a));
}

//! Test: Real FBX backend imports vertex colors when present.
/*!
 Scenario: A minimal ASCII FBX contains LayerElementColor mapped by polygon
 vertex with two distinct RGBA colors.

 Verifies: The emitted vertex buffer contains non-white vertex colors.
*/
NOLINT_TEST_F(
  FbxImporterGeometryTest, RealBackend_ImportsVertexColors_WhenPresent)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_geometry_vertex_colors");
  const auto source_path = temp_dir / "quad_vertex_colors.fbx";

  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 2\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Quad\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"QuadGeo\", \"Mesh\" {\n"
                          "    Vertices: *12 {\n"
                          "      a: 0,0,0,  1,0,0,  1,1,0,  0,1,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *6 {\n"
                          "      a: 0,1,-3,  0,2,-4\n"
                          "    }\n"
                          "    LayerElementColor: 0 {\n"
                          "      Version: 101\n"
                          "      Name: \"\"\n"
                          "      MappingInformationType: \"ByPolygonVertex\"\n"
                          "      ReferenceInformationType: \"IndexToDirect\"\n"
                          "      Colors: *8 {\n"
                          "        a: 1,0,0,1,  0,1,0,1\n"
                          "      }\n"
                          "      ColorIndex: *6 {\n"
                          "        a: 0,0,0,  1,1,1\n"
                          "      }\n"
                          "    }\n"
                          "    Layer: 0 {\n"
                          "      Version: 100\n"
                          "      LayerElement:  {\n"
                          "        Type: \"LayerElementColor\"\n"
                          "        TypedIndex: 0\n"
                          "      }\n"
                          "    }\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kGeometry;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  ASSERT_TRUE(report.success);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto geo_asset_opt = FindFirstGeometryAsset(inspection);
  ASSERT_TRUE(geo_asset_opt.has_value());
  const auto& geo_asset = *geo_asset_opt;

  const auto desc_path
    = report.cooked_root / std::filesystem::path(geo_asset.descriptor_relpath);
  const auto loaded = LoadGeometryDescriptor(desc_path);

  const auto buffers = LoadBuffersFromCooked(report.cooked_root, inspection);
  ASSERT_GT(buffers.table.size(), loaded.mesh_desc.info.standard.vertex_buffer);

  const auto vb_desc
    = buffers.table[loaded.mesh_desc.info.standard.vertex_buffer];
  ASSERT_GT(vb_desc.size_bytes, 0u);
  ASSERT_EQ(vb_desc.element_stride, sizeof(oxygen::data::Vertex));

  const auto vb_begin = static_cast<size_t>(vb_desc.data_offset);
  const auto vb_end = vb_begin + static_cast<size_t>(vb_desc.size_bytes);
  ASSERT_LE(vb_end, buffers.data.size());

  const auto* vptr = reinterpret_cast<const oxygen::data::Vertex*>(
    buffers.data.data() + vb_begin);
  const auto vertex_count = vb_desc.size_bytes / sizeof(oxygen::data::Vertex);
  ASSERT_GT(vertex_count, 0u);

  bool any_non_white = false;
  for (size_t i = 0; i < vertex_count; ++i) {
    const auto& c = vptr[i].color;
    const bool is_white = (std::abs(c.x - 1.0f) < 1e-4f)
      && (std::abs(c.y - 1.0f) < 1e-4f) && (std::abs(c.z - 1.0f) < 1e-4f)
      && (std::abs(c.w - 1.0f) < 1e-4f);
    if (!is_white) {
      any_non_white = true;
      break;
    }
  }

  EXPECT_TRUE(any_non_white);
}

} // namespace
