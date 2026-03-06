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
#include <ranges>
#include <span>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/InternalResourceKey.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/InputEvent.h>

#include "./AssetLoader_test.h"
#include "Fixtures/LooseCookedTestLayout.h"
#include "Utils/PakUtils.h"

using testing::IsNull;
using testing::NotNull;

using oxygen::content::internal::InternalResourceKey;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::InputMappingContextAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

using oxygen::content::testing::AssetLoaderLoadingTest;

namespace {

using oxygen::content::testing::LooseCookedLayout;

auto FillTestGuid(oxygen::data::loose_cooked::IndexHeader& header) -> void
{
  for (uint8_t i = 0; i < 16; ++i) {
    header.source_identity[i] = static_cast<uint8_t>(i + 1);
  }
}

auto NormalizePath(const std::filesystem::path& path) -> std::filesystem::path
{
  std::error_code ec;
  auto normalized = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    normalized = path.lexically_normal();
  }
  return normalized;
}

auto WriteMinimalLooseCookedIndex(const std::filesystem::path& cooked_root)
  -> void
{
  using oxygen::data::loose_cooked::IndexHeader;

  std::filesystem::create_directories(cooked_root);

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1; // "\0"
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(oxygen::data::loose_cooked::AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(oxygen::data::loose_cooked::FileRecord);

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
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::core::TextureResourceDesc;
  using oxygen::data::pak::render::MaterialAssetDesc;

  const LooseCookedLayout layout {};

  std::filesystem::create_directories(cooked_root / layout.materials_subdir);
  std::filesystem::create_directories(cooked_root / layout.resources_dir);

  // Arrange: write texture data
  const auto tex_payload
    = oxygen::content::testing::MakeV4TexturePayload(4U, std::byte { 0x11 });
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / layout.textures_data_file_name,
      std::ios::binary);
    out.write(reinterpret_cast<const char*>(tex_payload.data()),
      static_cast<std::streamsize>(tex_payload.size()));
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
  test_desc.size_bytes = static_cast<uint32_t>(tex_payload.size());
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
      cooked_root / layout.resources_dir / layout.textures_table_file_name,
      std::ios::binary);
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
  material_desc.header.version
    = oxygen::data::pak::render::kMaterialAssetVersion;
  material_desc.header.streaming_priority = 0;
  material_desc.header.content_hash = {};
  material_desc.header.variant_flags = 0;

  material_desc.material_domain = 0;
  material_desc.flags = 0;
  material_desc.shader_stages = 0;
  material_desc.base_color_texture
    = oxygen::data::pak::core::ResourceIndexT { 1u };

  {
    const auto material_file
      = LooseCookedLayout::MaterialDescriptorFileName("TestMaterial");

    const auto descriptor_relpath
      = std::filesystem::path(layout.materials_subdir) / material_file;
    std::ofstream out(cooked_root / descriptor_relpath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&material_desc),
      static_cast<std::streamsize>(sizeof(material_desc)));
  }

  // Arrange: build index string table
  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.materials_subdir) + "/"
    + LooseCookedLayout::MaterialDescriptorFileName("TestMaterial");
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += layout.MaterialVirtualPath("TestMaterial");
  strings.push_back('\0');
  const auto off_tex_table = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesTableRelPath();
  strings.push_back('\0');
  const auto off_tex_data = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesDataRelPath();
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
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
  tex_data_record.size = static_cast<uint64_t>(tex_payload.size());

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
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  const LooseCookedLayout layout {};

  std::filesystem::create_directories(cooked_root / layout.resources_dir);

  {
    std::ofstream out(
      cooked_root / layout.resources_dir / layout.textures_table_file_name,
      std::ios::binary);
    const char byte = 0x7f;
    out.write(&byte, 1);
  }
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / layout.textures_data_file_name,
      std::ios::binary);
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_tex_table = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesTableRelPath();
  strings.push_back('\0');
  const auto off_tex_data = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesDataRelPath();
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(oxygen::data::loose_cooked::AssetEntry);
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

auto ReadAssetHeader(oxygen::content::internal::IContentSource& source,
  const oxygen::data::AssetKey& key)
  -> std::optional<oxygen::data::pak::core::AssetHeader>
{
  auto desc_reader = source.CreateAssetDescriptorReader(key);
  if (!desc_reader) {
    return std::nullopt;
  }

  auto header_blob
    = desc_reader->ReadBlob(sizeof(oxygen::data::pak::core::AssetHeader));
  if (!header_blob
    || header_blob->size() < sizeof(oxygen::data::pak::core::AssetHeader)) {
    return std::nullopt;
  }

  oxygen::data::pak::core::AssetHeader header {};
  std::memcpy(&header, header_blob->data(), sizeof(header));
  return header;
}

auto WriteLooseCookedSceneForCatalog(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::world::SceneAssetDesc;

  const LooseCookedLayout layout {};
  std::filesystem::create_directories(cooked_root / layout.scenes_subdir);

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", "LooseScene");
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const auto rel_desc
    = std::filesystem::path(layout.scenes_subdir) / "LooseScene.scene";
  {
    std::ofstream out(cooked_root / rel_desc, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&desc), sizeof(desc));
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += rel_desc.generic_string();
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings
    += std::string(layout.virtual_mount_root) + "/" + rel_desc.generic_string();
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset + sizeof(AssetEntry);
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  AssetEntry asset_entry {};
  asset_entry.asset_key = key;
  asset_entry.descriptor_relpath_offset = off_desc;
  asset_entry.virtual_path_offset = off_vpath;
  asset_entry.asset_type = static_cast<uint8_t>(AssetType::kScene);
  asset_entry.descriptor_size = sizeof(SceneAssetDesc);

  std::ofstream out(cooked_root / "container.index.bin", std::ios::binary);
  out.write(reinterpret_cast<const char*>(&header),
    static_cast<std::streamsize>(sizeof(header)));
  out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  out.write(reinterpret_cast<const char*>(&asset_entry),
    static_cast<std::streamsize>(sizeof(asset_entry)));
}

auto WriteLooseCookedInputAssets(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& action_key,
  const oxygen::data::AssetKey& context_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::input::InputActionAssetDesc;
  using oxygen::data::pak::input::InputActionAssetFlags;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputMappingContextFlags;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  const auto input_dir = cooked_root / "Input";
  std::filesystem::create_directories(input_dir);

  const auto action_rel_desc = std::filesystem::path("Input") / "Move.oiact";
  InputActionAssetDesc action_desc {};
  action_desc.header.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
  std::snprintf(
    action_desc.header.name, sizeof(action_desc.header.name), "%s", "Move");
  action_desc.header.version
    = oxygen::data::pak::input::kInputActionAssetVersion;
  action_desc.value_type = 0;
  action_desc.flags = InputActionAssetFlags::kConsumesInput;
  {
    std::ofstream out(cooked_root / action_rel_desc, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&action_desc), sizeof(action_desc));
  }

  const auto context_rel_desc
    = std::filesystem::path("Input") / "Hydrated.oimap";
  InputMappingContextAssetDesc context_desc {};
  context_desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  std::snprintf(context_desc.header.name, sizeof(context_desc.header.name),
    "%s", "HydratedContext");
  context_desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  context_desc.flags = InputMappingContextFlags::kAutoLoad
    | InputMappingContextFlags::kAutoActivate;
  context_desc.default_priority = 77;

  constexpr const char kSlotName[] = "Space";
  const auto strings_size = static_cast<uint32_t>(sizeof(kSlotName));
  context_desc.mappings.offset = sizeof(InputMappingContextAssetDesc);
  context_desc.mappings.count = 1;
  context_desc.mappings.entry_size = sizeof(InputActionMappingRecord);
  context_desc.triggers.offset
    = context_desc.mappings.offset + sizeof(InputActionMappingRecord);
  context_desc.triggers.count = 1;
  context_desc.triggers.entry_size = sizeof(InputTriggerRecord);
  context_desc.trigger_aux.offset
    = context_desc.triggers.offset + sizeof(InputTriggerRecord);
  context_desc.trigger_aux.count = 0;
  context_desc.trigger_aux.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerAuxRecord);
  context_desc.strings.offset = context_desc.trigger_aux.offset;
  context_desc.strings.count = strings_size;
  context_desc.strings.entry_size = sizeof(char);

  InputActionMappingRecord mapping {};
  mapping.action_asset_key = action_key;
  mapping.slot_name_offset = 0;
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 1;
  mapping.scale[0] = 1.0F;
  mapping.scale[1] = 1.0F;
  mapping.bias[0] = 0.0F;
  mapping.bias[1] = 0.0F;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kPressed;
  trigger.actuation_threshold = 0.5F;

  const auto context_blob_size
    = static_cast<size_t>(context_desc.strings.offset)
    + static_cast<size_t>(context_desc.strings.count);
  std::vector<std::byte> context_blob(context_blob_size, std::byte { 0 });
  std::memcpy(context_blob.data(), &context_desc, sizeof(context_desc));
  std::memcpy(context_blob.data() + context_desc.mappings.offset, &mapping,
    sizeof(mapping));
  std::memcpy(context_blob.data() + context_desc.triggers.offset, &trigger,
    sizeof(trigger));
  std::memcpy(context_blob.data() + context_desc.strings.offset, kSlotName,
    sizeof(kSlotName));
  {
    std::ofstream out(cooked_root / context_rel_desc, std::ios::binary);
    out.write(reinterpret_cast<const char*>(context_blob.data()),
      static_cast<std::streamsize>(context_blob.size()));
  }

  std::string strings;
  strings.push_back('\0');
  const auto action_off_desc = static_cast<uint32_t>(strings.size());
  strings += action_rel_desc.generic_string();
  strings.push_back('\0');
  const auto action_off_vpath = static_cast<uint32_t>(strings.size());
  strings += std::string("/Game/") + action_rel_desc.generic_string();
  strings.push_back('\0');
  const auto context_off_desc = static_cast<uint32_t>(strings.size());
  strings += context_rel_desc.generic_string();
  strings.push_back('\0');
  const auto context_off_vpath = static_cast<uint32_t>(strings.size());
  strings += std::string("/Game/") + context_rel_desc.generic_string();
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 2;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset
    + static_cast<uint64_t>(sizeof(AssetEntry)) * 2U;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  AssetEntry action_entry {};
  action_entry.asset_key = action_key;
  action_entry.descriptor_relpath_offset = action_off_desc;
  action_entry.virtual_path_offset = action_off_vpath;
  action_entry.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
  action_entry.descriptor_size = sizeof(InputActionAssetDesc);

  AssetEntry context_entry {};
  context_entry.asset_key = context_key;
  context_entry.descriptor_relpath_offset = context_off_desc;
  context_entry.virtual_path_offset = context_off_vpath;
  context_entry.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  context_entry.descriptor_size = static_cast<uint64_t>(context_blob.size());

  std::ofstream out(cooked_root / "container.index.bin", std::ios::binary);
  out.write(reinterpret_cast<const char*>(&header),
    static_cast<std::streamsize>(sizeof(header)));
  out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  out.write(reinterpret_cast<const char*>(&action_entry),
    static_cast<std::streamsize>(sizeof(action_entry)));
  out.write(reinterpret_cast<const char*>(&context_entry),
    static_cast<std::streamsize>(sizeof(context_entry)));
}

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

//! Test: AssetLoader can load a simple material asset from PAK file
/*!
 Scenario: Creates a PAK file with a basic material asset and verifies that the
 AssetLoader can successfully load it.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest, LoadAssetSimpleMaterialLoadsSuccessfully)
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
    AssetLoader loader(Tag::Get(), config);

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
NOLINT_TEST_F(AssetLoaderLoadingTest, LoadAssetSimpleGeometryLoadsSuccessfully)
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
    AssetLoader loader(Tag::Get(), config);

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
  AssetLoaderLoadingTest, LoadAssetLooseCookedMaterialLoadsWithTexture)
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
    AssetLoader loader(Tag::Get(), config);

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
  AssetLoaderLoadingTest, LoadAssetLooseCookedMultipleRootsAssignsStableIds)
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
    AssetLoader loader(Tag::Get(), config);

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
NOLINT_TEST_F(AssetLoaderLoadingTest, AddLooseCookedRootInvalidTexturesTable)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_invalid_tex_table";
  WriteLooseCookedIndexWithInvalidTexturesTable(cooked_root);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::runtime_error);
}

//! Test: AssetLoader can load a geometry asset with buffer dependencies
/*!
 Scenario: Creates a PAK file with a geometry asset that has vertex and index
 buffer dependencies and verifies successful loading with proper mesh properties
 and buffer references.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest, LoadAssetComplexGeometryLoadsSuccessfully)
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
    AssetLoader loader(Tag::Get(), config);

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
NOLINT_TEST_F(AssetLoaderLoadingTest, LoadAssetNonExistentReturnsNull)
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
    AssetLoader loader(Tag::Get(), config);

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
  AssetLoaderLoadingTest, LoadAssetSameAssetTwiceReturnsSameInstance)
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
    AssetLoader loader(Tag::Get(), config);

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
  AssetLoaderLoadingTest, LoadAssetPakStillLoadsAfterLooseCookedRegistration)
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
    AssetLoader loader(Tag::Get(), config);

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
NOLINT_TEST_F(AssetLoaderLoadingTest, MakeResourceKeyPakIndexIgnoresLooseRoots)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  WriteMinimalLooseCookedIndex(cooked_root);
  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto pak_path = GeneratePakFile("simple_material");
  asset_loader_->AddPakFile(pak_path);

  const auto pak_file = oxygen::content::PakFile(pak_path);

  // Act
  const auto resource_key = asset_loader_->MakeResourceKey<BufferResource>(
    pak_file, oxygen::data::pak::core::ResourceIndexT { 0u });
  const auto decoded = InternalResourceKey(resource_key);

  // Assert
  EXPECT_EQ(decoded.GetPakIndex(), 0u);
}

//! Duplicate AssetKey conflict policy: newest mount wins by default.
NOLINT_TEST_F(
  AssetLoaderLoadingTest, DuplicateAssetKeyDefaultLookupUsesNewestMountedSource)
{
  const auto pak_a = GeneratePakFile("duplicate_key_source_a");
  const auto pak_b = GeneratePakFile("duplicate_key_source_b");
  const auto material_key = CreateTestAssetKey("duplicate_shared_material");

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_a);
      loader.AddPakFile(pak_b); // newest mount should win

      const auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, NotNull());
      if (material) {
        const auto base = material->GetBaseColor();
        EXPECT_FLOAT_EQ(base[0], 0.0F);
        EXPECT_FLOAT_EQ(base[1], 0.0F);
        EXPECT_FLOAT_EQ(base[2], 1.0F);
        EXPECT_FLOAT_EQ(base[3], 1.0F);
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Patch tombstones must block fallback to lower-priority base mounts.
NOLINT_TEST_F(AssetLoaderLoadingTest,
  PatchTombstonePreventsAssetFallbackToLowerPriorityMount)
{
  const auto pak_a = GeneratePakFile("duplicate_key_source_a");
  const auto pak_b = GeneratePakFile("duplicate_key_source_b");
  const auto material_key = CreateTestAssetKey("duplicate_shared_material");

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;
    using oxygen::data::PakCatalog;
    using oxygen::data::PatchManifest;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_a);

      PatchManifest manifest {};
      manifest.compatibility_policy_snapshot.require_exact_base_set = false;
      manifest.compatibility_policy_snapshot.require_content_version_match
        = false;
      manifest.compatibility_policy_snapshot.require_base_source_key_match
        = false;
      manifest.compatibility_policy_snapshot.require_catalog_digest_match
        = false;
      manifest.deleted.push_back(material_key);

      loader.AddPatchPakFile(pak_b, manifest, std::span<const PakCatalog> {});

      const auto material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(material, IsNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Characterization: duplicate-key resolution follows mount order.
/*!
 Scenario: Mount two sources that define the same Material AssetKey and
 assert lookup result changes when mount order is reversed. This captures
 current behavior where explicit load priority metadata is not part of the
 runtime load API surface.
*/
NOLINT_TEST_F(AssetLoaderLoadingTest,
  Characterization_DuplicateAssetLookupFollowsMountOrderNotPriority)
{
  const auto pak_a = GeneratePakFile("duplicate_key_source_a");
  const auto pak_b = GeneratePakFile("duplicate_key_source_b");
  const auto material_key = CreateTestAssetKey("duplicate_shared_material");

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_a);
      loader.AddPakFile(pak_b);
      auto newest_wins_material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(newest_wins_material, NotNull());
      if (!newest_wins_material) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      const auto newest_base = newest_wins_material->GetBaseColor();
      EXPECT_FLOAT_EQ(newest_base[0], 0.0F);
      EXPECT_FLOAT_EQ(newest_base[1], 0.0F);
      EXPECT_FLOAT_EQ(newest_base[2], 1.0F);
      EXPECT_FLOAT_EQ(newest_base[3], 1.0F);

      newest_wins_material.reset();
      (void)loader.ReleaseAsset(material_key);
      loader.TrimCache();
      loader.ClearMounts();

      loader.AddPakFile(pak_b);
      loader.AddPakFile(pak_a);
      auto reversed_order_material
        = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
      EXPECT_THAT(reversed_order_material, NotNull());
      if (!reversed_order_material) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      const auto reversed_base = reversed_order_material->GetBaseColor();
      EXPECT_FLOAT_EQ(reversed_base[0], 0.0F);
      EXPECT_FLOAT_EQ(reversed_base[1], 1.0F);
      EXPECT_FLOAT_EQ(reversed_base[2], 0.0F);
      EXPECT_FLOAT_EQ(reversed_base[3], 1.0F);

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Preferred-source override policy: dependency loads follow the parent source.
NOLINT_TEST_F(AssetLoaderLoadingTest,
  DuplicateAssetKeyGeometryDependenciesPreferGeometrySource)
{
  const auto pak_a = GeneratePakFile("duplicate_key_source_a");
  const auto pak_b = GeneratePakFile("duplicate_key_source_b");
  const auto geometry_key = CreateTestAssetKey("duplicate_source_a_geometry");

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_a); // geometry + green material
      loader.AddPakFile(pak_b); // duplicate material key with blue color

      const auto geometry
        = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_key);
      EXPECT_THAT(geometry, NotNull());
      if (geometry) {
        const auto meshes = geometry->Meshes();
        EXPECT_FALSE(meshes.empty());
        if (!meshes.empty() && meshes[0]) {
          const auto submeshes = meshes[0]->SubMeshes();
          EXPECT_FALSE(submeshes.empty());
          if (!submeshes.empty()) {
            const auto material = submeshes[0].Material();
            EXPECT_THAT(material, NotNull());
            if (material) {
              const auto base = material->GetBaseColor();
              EXPECT_FLOAT_EQ(base[0], 0.0F);
              EXPECT_FLOAT_EQ(base[1], 1.0F);
              EXPECT_FLOAT_EQ(base[2], 0.0F);
              EXPECT_FLOAT_EQ(base[3], 1.0F);
            }
          }
        }
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  EnumerateMountedSourcesMixedMountsExpectedToExposeRuntimeMountedState)
{
  const auto cooked_root = temp_dir_ / "mounted_sources_loose";
  WriteMinimalLooseCookedIndex(cooked_root);
  const auto pak_path = GeneratePakFile("simple_material");

  asset_loader_->AddPakFile(pak_path);
  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto mounted_sources = asset_loader_->EnumerateMountedSources();
  ASSERT_EQ(mounted_sources.size(), 2U);

  bool saw_pak = false;
  bool saw_loose = false;
  for (const auto& source : mounted_sources) {
    if (source.source_kind
        == oxygen::content::IAssetLoader::ContentSourceKind::kPak
      && source.source_path == NormalizePath(pak_path)) {
      saw_pak = true;
    }
    if (source.source_kind
        == oxygen::content::IAssetLoader::ContentSourceKind::kLooseCooked
      && source.source_path == NormalizePath(cooked_root)) {
      saw_loose = true;
    }
  }

  EXPECT_TRUE(saw_pak);
  EXPECT_TRUE(saw_loose);
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  EnumerateMountedSourcesRemountExpectedToRefreshNotDuplicateMountedSource)
{
  const auto pak_path = GeneratePakFile("simple_material");

  asset_loader_->AddPakFile(pak_path);
  const auto first_mount_snapshot = asset_loader_->EnumerateMountedSources();
  ASSERT_EQ(first_mount_snapshot.size(), 1U);

  asset_loader_->AddPakFile(pak_path);
  const auto second_mount_snapshot = asset_loader_->EnumerateMountedSources();
  ASSERT_EQ(second_mount_snapshot.size(), 1U);
  EXPECT_EQ(second_mount_snapshot.front().source_kind,
    oxygen::content::IAssetLoader::ContentSourceKind::kPak);
  EXPECT_EQ(second_mount_snapshot.front().source_path, NormalizePath(pak_path));
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  EnumerateMountedScenesMixedSourcesExpectedToExposeSceneEntries)
{
  const auto pak_path = GeneratePakFile("scene_with_renderable");
  const auto pak_scene_key = CreateTestAssetKey("test_scene");

  const auto loose_scene_key = CreateTestAssetKey("loose_scene_catalog");
  const auto cooked_root = temp_dir_ / "mounted_scenes_loose";
  WriteLooseCookedSceneForCatalog(cooked_root, loose_scene_key);

  asset_loader_->AddPakFile(pak_path);
  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto mounted_scenes = asset_loader_->EnumerateMountedScenes();
  ASSERT_GE(mounted_scenes.size(), 2U);

  bool saw_pak_scene = false;
  bool saw_loose_scene = false;
  for (const auto& scene : mounted_scenes) {
    if (scene.scene_key == pak_scene_key
      && scene.source_kind
        == oxygen::content::IAssetLoader::ContentSourceKind::kPak
      && scene.source_path == NormalizePath(pak_path)) {
      saw_pak_scene = true;
    }
    if (scene.scene_key == loose_scene_key
      && scene.source_kind
        == oxygen::content::IAssetLoader::ContentSourceKind::kLooseCooked
      && scene.source_path == NormalizePath(cooked_root)
      && !scene.virtual_path.empty()) {
      saw_loose_scene = true;
    }
  }

  EXPECT_TRUE(saw_pak_scene);
  EXPECT_TRUE(saw_loose_scene);
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  EnumerateMountedInputContextsLooseCookedExpectedToExposeEntries)
{
  const auto action_key = CreateTestAssetKey("loose_input_action_catalog");
  const auto context_key = CreateTestAssetKey("loose_input_context_catalog");
  const auto cooked_root = temp_dir_ / "mounted_input_contexts_loose";
  WriteLooseCookedInputAssets(cooked_root, action_key, context_key);

  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto mounted_contexts = asset_loader_->EnumerateMountedInputContexts();
  ASSERT_FALSE(mounted_contexts.empty());

  const auto found = std::ranges::find_if(mounted_contexts,
    [&](const auto& entry) { return entry.asset_key == context_key; });
  ASSERT_NE(found, mounted_contexts.end());
  EXPECT_EQ(found->name, "HydratedContext");
  EXPECT_EQ(found->default_priority, 77);
  EXPECT_EQ((found->flags
              & oxygen::data::pak::input::InputMappingContextFlags::kAutoLoad),
    oxygen::data::pak::input::InputMappingContextFlags::kAutoLoad);
  EXPECT_EQ(
    (found->flags
      & oxygen::data::pak::input::InputMappingContextFlags::kAutoActivate),
    oxygen::data::pak::input::InputMappingContextFlags::kAutoActivate);
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  HydrateInputContextExpectedToRegisterActionsAndBuildMappingContext)
{
  const auto action_key = CreateTestAssetKey("loose_input_action_hydrate");
  const auto context_key = CreateTestAssetKey("loose_input_context_hydrate");
  const auto cooked_root = temp_dir_ / "hydrate_input_context_loose";
  WriteLooseCookedInputAssets(cooked_root, action_key, context_key);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    oxygen::co::BroadcastChannel<oxygen::platform::InputEvent> input_channel;
    oxygen::engine::InputSystem input_system(input_channel.ForRead());

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddLooseCookedRoot(cooked_root);

      const auto context_asset
        = co_await loader.LoadAssetAsync<InputMappingContextAsset>(context_key);
      EXPECT_THAT(context_asset, NotNull());
      EXPECT_THAT(loader.GetInputActionAsset(action_key), NotNull());

      if (context_asset) {
        const auto hydrated = oxygen::content::HydrateInputContext(
          *context_asset, loader, input_system);
        EXPECT_THAT(hydrated, NotNull());
        if (hydrated) {
          EXPECT_EQ(hydrated->GetName(), "HydratedContext");
        }

        const auto action = input_system.GetActionByName("Move");
        EXPECT_THAT(action, NotNull());
        if (action) {
          EXPECT_TRUE(action->ConsumesInput());
        }
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  TrimCacheExpectedToPreserveMountedCatalogClearMountsExpectedToClearCatalog)
{
  const auto loose_scene_key = CreateTestAssetKey("loose_scene_catalog_2");
  const auto cooked_root = temp_dir_ / "mounted_scenes_loose_2";
  WriteLooseCookedSceneForCatalog(cooked_root, loose_scene_key);

  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto sources_before = asset_loader_->EnumerateMountedSources();
  const auto scenes_before = asset_loader_->EnumerateMountedScenes();
  ASSERT_FALSE(sources_before.empty());
  ASSERT_FALSE(scenes_before.empty());

  asset_loader_->TrimCache();

  const auto sources_after_trim = asset_loader_->EnumerateMountedSources();
  const auto scenes_after_trim = asset_loader_->EnumerateMountedScenes();
  EXPECT_EQ(sources_after_trim.size(), sources_before.size());
  EXPECT_EQ(scenes_after_trim.size(), scenes_before.size());

  asset_loader_->ClearMounts();
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
  EXPECT_TRUE(asset_loader_->EnumerateMountedScenes().empty());
}

NOLINT_TEST_F(AssetLoaderLoadingTest,
  ContentSourceConformanceBufferTextureCapabilitiesExpectedToMatch)
{
  const auto material_key = CreateTestAssetKey("test_material");
  const auto pak_path = GeneratePakFile("simple_material");

  const auto cooked_root = temp_dir_ / "conformance_loose_material";
  WriteLooseCookedMaterialWithTexture(cooked_root, material_key);

  oxygen::content::internal::PakFileSource pak_source(pak_path, false);
  oxygen::content::internal::LooseCookedSource loose_source(cooked_root, false);

  auto assert_common = [&](oxygen::content::internal::IContentSource& source) {
    EXPECT_TRUE(source.HasAsset(material_key));

    const auto header_opt = ReadAssetHeader(source, material_key);
    ASSERT_TRUE(header_opt.has_value());
    EXPECT_EQ(static_cast<oxygen::data::AssetType>(header_opt->asset_type),
      oxygen::data::AssetType::kMaterial);

    EXPECT_THAT(source.CreateTextureTableReader(), NotNull());
    EXPECT_THAT(source.CreateTextureDataReader(), NotNull());
    EXPECT_THAT(source.GetTextureTable(), NotNull());
    EXPECT_EQ(source.GetScriptTable(), nullptr);
    EXPECT_EQ(source.GetPhysicsTable(), nullptr);
    EXPECT_EQ(source.CreateScriptTableReader(), nullptr);
    EXPECT_EQ(source.CreateScriptDataReader(), nullptr);
    EXPECT_EQ(source.CreatePhysicsTableReader(), nullptr);
    EXPECT_EQ(source.CreatePhysicsDataReader(), nullptr);

    const auto has_buffer_table_reader
      = static_cast<bool>(source.CreateBufferTableReader());
    const auto has_buffer_data_reader
      = static_cast<bool>(source.CreateBufferDataReader());
    const auto has_buffer_table = source.GetBufferTable() != nullptr;
    EXPECT_EQ(has_buffer_table_reader, has_buffer_data_reader);
    EXPECT_EQ(has_buffer_table_reader, has_buffer_table);
  };

  assert_common(pak_source);
  assert_common(loose_source);
}

} // namespace
