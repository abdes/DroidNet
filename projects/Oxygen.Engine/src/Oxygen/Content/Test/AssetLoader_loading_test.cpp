//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "./AssetLoader_test.h"
#include <Oxygen/Base/ObserverPtr.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Internal/InternalResourceKey.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

using testing::IsNull;
using testing::NotNull;

using oxygen::content::internal::InternalResourceKey;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

using oxygen::content::testing::AssetLoaderLoadingTest;

namespace {

auto FillTestGuid(oxygen::data::loose_cooked::v1::IndexHeader& header) -> void
{
  for (uint8_t i = 0; i < 16; ++i) {
    header.guid[i] = static_cast<uint8_t>(i + 1);
  }
}

auto WriteMinimalLooseCookedIndex(const std::filesystem::path& cooked_root)
  -> void
{
  using oxygen::data::loose_cooked::v1::IndexHeader;

  std::filesystem::create_directories(cooked_root);

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::v1::kHasVirtualPaths
    | oxygen::data::loose_cooked::v1::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1; // "\0"
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(oxygen::data::loose_cooked::v1::AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(oxygen::data::loose_cooked::v1::FileRecord);

  const auto index_path = cooked_root / "container.index.bin";
  std::ofstream out(index_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  const char zero = 0;
  out.write(&zero, 1);
}

auto WriteLooseCookedMaterialWithTexture(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& asset_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::v1::AssetEntry;
  using oxygen::data::loose_cooked::v1::FileKind;
  using oxygen::data::loose_cooked::v1::FileRecord;
  using oxygen::data::loose_cooked::v1::IndexHeader;
  using oxygen::data::pak::MaterialAssetDesc;
  using oxygen::data::pak::TextureResourceDesc;

  std::filesystem::create_directories(cooked_root / "assets");
  std::filesystem::create_directories(cooked_root / "resources");

  // Arrange: write texture data
  const std::vector<std::byte> tex_data = {
    std::byte { 0x11 },
    std::byte { 0x22 },
    std::byte { 0x33 },
    std::byte { 0x44 },
  };
  {
    std::ofstream out(
      cooked_root / "resources" / "textures.data", std::ios::binary);
    out.write(reinterpret_cast<const char*>(tex_data.data()),
      static_cast<std::streamsize>(tex_data.size()));
  }

  // Arrange: write texture table (2 entries: fallback + test texture)
  TextureResourceDesc fallback_desc {};
  fallback_desc.data_offset = 0;
  fallback_desc.size_bytes = 0;
  fallback_desc.texture_type = 3; // TextureType::kTexture2D
  fallback_desc.compression_type = 0;
  fallback_desc.width = 1;
  fallback_desc.height = 1;
  fallback_desc.depth = 1;
  fallback_desc.array_layers = 1;
  fallback_desc.mip_levels = 1;
  fallback_desc.format = 0;
  fallback_desc.alignment = 256;

  TextureResourceDesc test_desc {};
  test_desc.data_offset = 0;
  test_desc.size_bytes = static_cast<uint32_t>(tex_data.size());
  test_desc.texture_type = 3; // TextureType::kTexture2D
  test_desc.compression_type = 0;
  test_desc.width = 1;
  test_desc.height = 1;
  test_desc.depth = 1;
  test_desc.array_layers = 1;
  test_desc.mip_levels = 1;
  test_desc.format = 0;
  test_desc.alignment = 256;

  {
    std::ofstream out(
      cooked_root / "resources" / "textures.table", std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fallback_desc),
      static_cast<std::streamsize>(sizeof(fallback_desc)));
    out.write(reinterpret_cast<const char*>(&test_desc),
      static_cast<std::streamsize>(sizeof(test_desc)));
  }

  // Arrange: write material descriptor referencing texture index 1
  MaterialAssetDesc material_desc {};
  material_desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
  std::snprintf(material_desc.header.name, sizeof(material_desc.header.name),
    "%s", "TestMaterial");
  material_desc.header.version = 1;
  material_desc.header.streaming_priority = 0;
  material_desc.header.content_hash = 0;
  material_desc.header.variant_flags = 0;

  material_desc.material_domain = 0;
  material_desc.flags = 0;
  material_desc.shader_stages = 0;
  material_desc.base_color_texture = 1;

  {
    std::ofstream out(
      cooked_root / "assets" / "TestMaterial.mat", std::ios::binary);
    out.write(reinterpret_cast<const char*>(&material_desc),
      static_cast<std::streamsize>(sizeof(material_desc)));
  }

  // Arrange: build index string table
  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "assets/TestMaterial.mat";
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "/Content/TestMaterial.mat";
  strings.push_back('\0');
  const auto off_tex_table = static_cast<uint32_t>(strings.size());
  strings += "resources/textures.table";
  strings.push_back('\0');
  const auto off_tex_data = static_cast<uint32_t>(strings.size());
  strings += "resources/textures.data";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::v1::kHasVirtualPaths
    | oxygen::data::loose_cooked::v1::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 2;
  header.file_record_size = sizeof(FileRecord);

  AssetEntry asset_entry {};
  asset_entry.asset_key = asset_key;
  asset_entry.descriptor_relpath_offset = off_desc;
  asset_entry.virtual_path_offset = off_vpath;
  asset_entry.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
  asset_entry.descriptor_size = sizeof(MaterialAssetDesc);

  FileRecord tex_table_record {};
  tex_table_record.kind = FileKind::kTexturesTable;
  tex_table_record.relpath_offset = off_tex_table;
  tex_table_record.size = sizeof(TextureResourceDesc) * 2;

  FileRecord tex_data_record {};
  tex_data_record.kind = FileKind::kTexturesData;
  tex_data_record.relpath_offset = off_tex_data;
  tex_data_record.size = static_cast<uint64_t>(tex_data.size());

  const auto index_path = cooked_root / "container.index.bin";
  std::ofstream index_out(index_path, std::ios::binary);
  index_out.write(reinterpret_cast<const char*>(&header),
    static_cast<std::streamsize>(sizeof(header)));
  index_out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  index_out.write(reinterpret_cast<const char*>(&asset_entry),
    static_cast<std::streamsize>(sizeof(asset_entry)));
  index_out.write(reinterpret_cast<const char*>(&tex_table_record),
    static_cast<std::streamsize>(sizeof(tex_table_record)));
  index_out.write(reinterpret_cast<const char*>(&tex_data_record),
    static_cast<std::streamsize>(sizeof(tex_data_record)));
}

auto WriteLooseCookedIndexWithInvalidTexturesTable(
  const std::filesystem::path& cooked_root) -> void
{
  using oxygen::data::loose_cooked::v1::FileKind;
  using oxygen::data::loose_cooked::v1::FileRecord;
  using oxygen::data::loose_cooked::v1::IndexHeader;

  std::filesystem::create_directories(cooked_root / "resources");

  {
    std::ofstream out(
      cooked_root / "resources" / "textures.table", std::ios::binary);
    const char byte = 0x7f;
    out.write(&byte, 1);
  }
  {
    std::ofstream out(
      cooked_root / "resources" / "textures.data", std::ios::binary);
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_tex_table = static_cast<uint32_t>(strings.size());
  strings += "resources/textures.table";
  strings.push_back('\0');
  const auto off_tex_data = static_cast<uint32_t>(strings.size());
  strings += "resources/textures.data";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::v1::kHasVirtualPaths
    | oxygen::data::loose_cooked::v1::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(oxygen::data::loose_cooked::v1::AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 2;
  header.file_record_size = sizeof(FileRecord);

  FileRecord tex_table_record {};
  tex_table_record.kind = FileKind::kTexturesTable;
  tex_table_record.relpath_offset = off_tex_table;
  tex_table_record.size = 1;

  FileRecord tex_data_record {};
  tex_data_record.kind = FileKind::kTexturesData;
  tex_data_record.relpath_offset = off_tex_data;
  tex_data_record.size = 0;

  const auto index_path = cooked_root / "container.index.bin";
  std::ofstream out(index_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(&header),
    static_cast<std::streamsize>(sizeof(header)));
  out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  out.write(reinterpret_cast<const char*>(&tex_table_record),
    static_cast<std::streamsize>(sizeof(tex_table_record)));
  out.write(reinterpret_cast<const char*>(&tex_data_record),
    static_cast<std::streamsize>(sizeof(tex_data_record)));
}

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

//! Test: AssetLoader can load a simple material asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic material asset and verifies that the
 AssetLoader can successfully load it.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_SimpleMaterial_LoadsSuccessfully)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_material");
  const auto material_key = CreateTestAssetKey("test_material");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader can load a simple geometry asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic geometry asset and verifies that the
 AssetLoader can successfully load it.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_SimpleGeometry_LoadsSuccessfully)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_geometry");
  const auto geometry_key = CreateTestAssetKey("test_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
      EXPECT_THAT(geometry, NotNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader can load a material from a loose cooked root
/*!
 Scenario: Writes a minimal loose cooked container containing a material
 descriptor and a texture table/data pair, mounts it, and verifies that the
 material loads and the referenced texture resource is cached.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_LooseCookedMaterial_LoadsWithTexture)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked";
  const auto material_key = CreateTestAssetKey("loose_material");
  WriteLooseCookedMaterialWithTexture(cooked_root, material_key);

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddLooseCookedRoot(cooked_root);

      const auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());

      if (material) {
        const auto base_color_key = material->GetBaseColorTextureKey();
        EXPECT_NE(base_color_key.get(), 0U);
        EXPECT_THAT(
          loader.GetResource<TextureResource>(base_color_key), NotNull());
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Loose cooked container ids are assigned deterministically
/*!
 Scenario: Mounts two loose cooked roots and loads a material from each.
 Verifies that each material's texture dependency is cached and that the
 resulting runtime ResourceKeys differ across distinct sources.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_LooseCookedMultipleRoots_AssignsStableIds)
{
  // Arrange
  const auto cooked_root_a = temp_dir_ / "loose_cooked_a";
  const auto cooked_root_b = temp_dir_ / "loose_cooked_b";

  const auto material_key_a = CreateTestAssetKey("loose_material_a");
  const auto material_key_b = CreateTestAssetKey("loose_material_b");

  WriteLooseCookedMaterialWithTexture(cooked_root_a, material_key_a);
  WriteLooseCookedMaterialWithTexture(cooked_root_b, material_key_b);

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddLooseCookedRoot(cooked_root_a);
      loader.AddLooseCookedRoot(cooked_root_b);

      const auto material_a
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key_a);
      const auto material_b
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key_b);

      EXPECT_THAT(material_a, NotNull());
      EXPECT_THAT(material_b, NotNull());

      if (material_a && material_b) {
        const auto tex_key_a = material_a->GetBaseColorTextureKey();
        const auto tex_key_b = material_b->GetBaseColorTextureKey();

        EXPECT_NE(tex_key_a.get(), 0U);
        EXPECT_NE(tex_key_b.get(), 0U);
        EXPECT_NE(tex_key_a.get(), tex_key_b.get());

        EXPECT_THAT(loader.GetResource<TextureResource>(tex_key_a), NotNull());
        EXPECT_THAT(loader.GetResource<TextureResource>(tex_key_b), NotNull());
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Mount fails when textures.table is not a multiple of entry size
/*!
 Scenario: Writes a loose cooked root with a `textures.table` whose size is not
 a multiple of `sizeof(TextureResourceDesc)`. Verifies mount rejects it.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest, AddLooseCookedRoot_InvalidTexturesTable)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_invalid_tex_table";
  WriteLooseCookedIndexWithInvalidTexturesTable(cooked_root);

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::runtime_error);
}

//! Test: AssetLoader can load a geometry asset with buffer dependencies
/*!
 Scenario: Creates a PAK file with a geometry asset that has vertex and index
 buffer dependencies and verifies successful loading with proper mesh properties
 and buffer references.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_ComplexGeometry_LoadsSuccessfully)
{
  // Arrange
  const auto pak_path = GeneratePakFile("complex_geometry");
  const auto geometry_key = CreateTestAssetKey("complex_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
      EXPECT_THAT(geometry, NotNull());

      if (geometry) {
        const auto meshes = geometry->Meshes();
        EXPECT_FALSE(meshes.empty());

        for (size_t i = 0; i < meshes.size(); ++i) {
          const auto& mesh = meshes[i];
          EXPECT_THAT(mesh, NotNull())
            << "Mesh at index " << i << " should not be null";

          if (mesh) {
            EXPECT_GE(mesh->VertexCount(), 0) << "Vertex count for mesh " << i;
            EXPECT_GE(mesh->IndexCount(), 0) << "Index count for mesh " << i;
          }
        }
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader returns nullptr for non-existent asset
/*!
 Scenario: Attempts to load an asset that doesn't exist in any PAK file and
 verifies that nullptr is returned.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest, LoadAsset_NonExistent_ReturnsNull)
{
  // Arrange
  const auto non_existent_key = CreateTestAssetKey("non_existent_asset");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      const auto result
        = co_await loader.LoadAssetAsync<MaterialAsset>(non_existent_key);
      EXPECT_THAT(result, IsNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader caches loaded assets
/*!
 Scenario: Loads the same asset twice and verifies that the same instance is
 returned (caching behavior).
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_SameAssetTwice_ReturnsSameInstance)
{
  // Arrange
  const auto pak_path = GeneratePakFile("simple_material");
  const auto material_key = CreateTestAssetKey("test_material");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto material1
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      const auto material2
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);

      EXPECT_THAT(material1, NotNull());
      EXPECT_THAT(material2, NotNull());
      EXPECT_EQ(material1.get(), material2.get());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Loose cooked sources do not break PAK discovery
/*!
 Scenario: Registers a loose cooked root before adding a PAK and verifies that
 PAK-backed assets are still discovered and loaded correctly.
*/
NOLINT_TEST_F(
  AssetLoaderLoadingTest, LoadAsset_PakStillLoadsAfterLooseCookedRegistration)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  WriteMinimalLooseCookedIndex(cooked_root);
  const auto pak_path = GeneratePakFile("simple_material");
  const auto material_key = CreateTestAssetKey("test_material");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddLooseCookedRoot(cooked_root);
      loader.AddPakFile(pak_path);

      const auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Loose cooked roots do not consume dense PAK index space
/*!
 Scenario: Registers a loose cooked root before adding a PAK and then composes
 a ResourceKey from that PAK. Verifies that the encoded PAK index remains 0 for
 the first added PAK, preserving deterministic ResourceKey encoding.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest, MakeResourceKey_PakIndexIgnoresLooseRoots)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  WriteMinimalLooseCookedIndex(cooked_root);
  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  const auto pak_file = oxygen::content::PakFile(pak_path);

  // Act
  const auto resource_key
    = asset_loader_->MakeResourceKey<BufferResource>(pak_file, 0u);
  const auto decoded = InternalResourceKey(resource_key);

  // Assert
  EXPECT_EQ(decoded.GetPakIndex(), 0u);
}

} // namespace
