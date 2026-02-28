//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

#include "./AssetLoader_test.h"
#include "Fixtures/LooseCookedTestLayout.h"

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderLoadingTest;
using oxygen::content::testing::LooseCookedLayout;
using oxygen::data::SceneAsset;
using oxygen::data::ScriptAsset;
using oxygen::data::ScriptResource;

namespace {

auto FillTestGuid(oxygen::data::loose_cooked::IndexHeader& header) -> void
{
  for (uint8_t i = 0; i < 16; ++i) {
    header.guid[i] = static_cast<uint8_t>(i + 1);
  }
}

auto HexNibble(const char c) -> uint8_t
{
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(10 + (c - 'a'));
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(10 + (c - 'A'));
  }
  throw std::runtime_error("invalid hex digit in asset key");
}

auto AssetKeyFromHex(std::string_view input) -> oxygen::data::AssetKey
{
  std::string hex;
  hex.reserve(32);
  for (const char c : input) {
    if (c == '-') {
      continue;
    }
    hex.push_back(c);
  }
  if (hex.size() != 32) {
    throw std::runtime_error("asset key must contain exactly 32 hex digits");
  }

  auto key_bytes = std::array<uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  for (size_t i = 0; i < key_bytes.size(); ++i) {
    const uint8_t hi = HexNibble(hex[2 * i]);
    const uint8_t lo = HexNibble(hex[2 * i + 1]);
    key_bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return oxygen::data::AssetKey::FromBytes(key_bytes);
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

auto WriteLooseCookedScriptAsset(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& script_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::scripting::ScriptAssetDesc;
  using oxygen::data::pak::scripting::ScriptResourceDesc;

  const LooseCookedLayout layout {};
  std::filesystem::create_directories(cooked_root / "Scripts");
  std::filesystem::create_directories(cooked_root / layout.resources_dir);

  ScriptAssetDesc script_desc {};
  script_desc.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
  std::snprintf(
    script_desc.header.name, sizeof(script_desc.header.name), "%s", "Reload");
  script_desc.bytecode_resource_index
    = oxygen::data::pak::core::ResourceIndexT { 0 };
  script_desc.source_resource_index = oxygen::data::pak::core::kNoResourceIndex;

  const auto desc_rel = std::filesystem::path("Scripts") / "Reload.oscript";
  {
    std::ofstream out(cooked_root / desc_rel, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&script_desc), sizeof(script_desc));
  }

  constexpr std::array<uint8_t, 8> kBytecode
    = { 0x4D, 0x4F, 0x56, 0x45, 0x42, 0x43, 0xA1, 0xA2 };
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / "scripts.data", std::ios::binary);
    out.write(reinterpret_cast<const char*>(kBytecode.data()),
      static_cast<std::streamsize>(kBytecode.size()));
  }

  ScriptResourceDesc resource_desc {};
  resource_desc.data_offset = 0;
  resource_desc.size_bytes = static_cast<uint32_t>(kBytecode.size());
  resource_desc.content_hash = 0; // disable hash verification in this fixture
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / "scripts.table", std::ios::binary);
    out.write(
      reinterpret_cast<const char*>(&resource_desc), sizeof(resource_desc));
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += desc_rel.generic_string();
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings
    += std::string(layout.virtual_mount_root) + "/" + desc_rel.generic_string();
  strings.push_back('\0');
  const auto off_scripts_table = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.resources_dir) + "/scripts.table";
  strings.push_back('\0');
  const auto off_scripts_data = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.resources_dir) + "/scripts.data";
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
  asset_entry.asset_key = script_key;
  asset_entry.descriptor_relpath_offset = off_desc;
  asset_entry.virtual_path_offset = off_vpath;
  asset_entry.asset_type = static_cast<uint8_t>(AssetType::kScript);
  asset_entry.descriptor_size = sizeof(ScriptAssetDesc);

  FileRecord scripts_table {};
  scripts_table.kind = FileKind::kScriptsTable;
  scripts_table.relpath_offset = off_scripts_table;
  scripts_table.size = sizeof(ScriptResourceDesc);

  FileRecord scripts_data {};
  scripts_data.kind = FileKind::kScriptsData;
  scripts_data.relpath_offset = off_scripts_data;
  scripts_data.size = kBytecode.size();

  std::ofstream index_out(
    cooked_root / "container.index.bin", std::ios::binary);
  index_out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  index_out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  index_out.write(
    reinterpret_cast<const char*>(&asset_entry), sizeof(asset_entry));
  index_out.write(
    reinterpret_cast<const char*>(&scripts_table), sizeof(scripts_table));
  index_out.write(
    reinterpret_cast<const char*>(&scripts_data), sizeof(scripts_data));
}

auto WriteLooseCookedSceneWithScripting(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& scene_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::scripting::ScriptingComponentRecord;
  using oxygen::data::pak::scripting::ScriptSlotRecord;
  using oxygen::data::pak::world::NodeRecord;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::data::pak::world::SceneComponentTableDesc;
  using oxygen::data::pak::world::SceneEnvironmentBlockHeader;

  const LooseCookedLayout layout {};
  std::filesystem::create_directories(cooked_root / layout.scenes_subdir);
  std::filesystem::create_directories(cooked_root / layout.resources_dir);

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  std::snprintf(
    desc.header.name, sizeof(desc.header.name), "%s", "ScriptScene");
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const uint32_t offset_nodes = sizeof(SceneAssetDesc);
  const uint32_t offset_strings = offset_nodes + sizeof(NodeRecord);
  static constexpr char kStrings[] = "\0root\0";
  const uint32_t strings_size = sizeof(kStrings) - 1;
  const uint32_t offset_dir = offset_strings + strings_size;
  const uint32_t offset_table = offset_dir + sizeof(SceneComponentTableDesc);
  const uint32_t offset_env = offset_table + sizeof(ScriptingComponentRecord);

  desc.nodes.offset = offset_nodes;
  desc.nodes.count = 1;
  desc.nodes.entry_size = sizeof(NodeRecord);
  desc.scene_strings.offset = offset_strings;
  desc.scene_strings.size = strings_size;
  desc.component_table_directory_offset = offset_dir;
  desc.component_table_count = 1;

  NodeRecord node {};
  node.node_id = scene_key;
  node.scene_name_offset = 1;
  node.parent_index = 0;

  SceneComponentTableDesc component_desc {};
  component_desc.component_type
    = static_cast<uint32_t>(oxygen::data::ComponentType::kScripting);
  component_desc.table.offset = offset_table;
  component_desc.table.count = 1;
  component_desc.table.entry_size = sizeof(ScriptingComponentRecord);

  ScriptingComponentRecord scripting {};
  scripting.node_index = 0;
  scripting.slot_start_index = 0;
  scripting.slot_count = 1;

  SceneEnvironmentBlockHeader env {};
  env.byte_size = sizeof(SceneEnvironmentBlockHeader);
  env.systems_count = 0;

  std::vector<std::byte> bytes(offset_env + sizeof(env));
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  std::memcpy(bytes.data() + offset_nodes, &node, sizeof(node));
  std::memcpy(bytes.data() + offset_strings, kStrings, strings_size);
  std::memcpy(
    bytes.data() + offset_dir, &component_desc, sizeof(component_desc));
  std::memcpy(bytes.data() + offset_table, &scripting, sizeof(scripting));
  std::memcpy(bytes.data() + offset_env, &env, sizeof(env));

  const auto rel_desc
    = std::filesystem::path(layout.scenes_subdir) / "ScriptScene.scene";
  {
    std::ofstream out(cooked_root / rel_desc, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
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
  const auto off_scripts_table = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.resources_dir) + "/scripts.table";
  strings.push_back('\0');
  const auto off_scripts_data = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.resources_dir) + "/scripts.data";
  strings.push_back('\0');

  ScriptSlotRecord slot {};
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / "scripts.table", std::ios::binary);
    out.write(reinterpret_cast<const char*>(&slot), sizeof(slot));
  }
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / "scripts.data", std::ios::binary);
  }

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
  header.file_record_count = 2;
  header.file_record_size = sizeof(oxygen::data::loose_cooked::FileRecord);

  AssetEntry asset_entry {};
  asset_entry.asset_key = scene_key;
  asset_entry.descriptor_relpath_offset = off_desc;
  asset_entry.virtual_path_offset = off_vpath;
  asset_entry.asset_type = static_cast<uint8_t>(AssetType::kScene);
  asset_entry.descriptor_size = static_cast<uint64_t>(bytes.size());

  FileRecord scripts_table_record {};
  scripts_table_record.kind = FileKind::kScriptsTable;
  scripts_table_record.relpath_offset = off_scripts_table;
  scripts_table_record.size = sizeof(ScriptSlotRecord);

  FileRecord scripts_data_record {};
  scripts_data_record.kind = FileKind::kScriptsData;
  scripts_data_record.relpath_offset = off_scripts_data;
  scripts_data_record.size = 0;

  std::ofstream index_out(
    cooked_root / "container.index.bin", std::ios::binary);
  index_out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  index_out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  index_out.write(
    reinterpret_cast<const char*>(&asset_entry), sizeof(asset_entry));
  index_out.write(reinterpret_cast<const char*>(&scripts_table_record),
    sizeof(scripts_table_record));
  index_out.write(reinterpret_cast<const char*>(&scripts_data_record),
    sizeof(scripts_data_record));
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

class AssetLoaderScriptingTest : public AssetLoaderLoadingTest { };

// P0.1.1: Source capability parity characterization.
NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetLooseCookedScriptResourceGenericLoadExpectedToSucceed)
{
  const auto script_key
    = AssetKeyFromHex("11111111-1111-1111-1111-111111111111");
  const auto cooked_root = temp_dir_ / "loose_script";
  WriteLooseCookedScriptAsset(cooked_root, script_key);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadScriptAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadScriptResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(cooked_root);

      const auto script_asset
        = co_await loader.LoadAssetAsync<ScriptAsset>(script_key);
      EXPECT_THAT(script_asset, NotNull());

      const auto key = loader.MakeScriptResourceKeyForAsset(
        script_key, oxygen::data::pak::core::ResourceIndexT { 0 });
      EXPECT_TRUE(key.has_value());
      if (!key.has_value()) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      const auto script_resource
        = co_await loader.LoadResourceAsync<ScriptResource>(*key);

      // Desired behavior contract (currently failing): loose-cooked script
      // resources should load through generic pipeline.
      EXPECT_THAT(script_resource, NotNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

// P0.1.2: Scene scripting dependency behavior characterization on loose roots.
NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetLooseCookedSceneWithScriptingExpectedToLoad)
{
  const auto scene_key
    = AssetKeyFromHex("22222222-2222-2222-2222-222222222222");
  const auto cooked_root = temp_dir_ / "loose_scene_scripting";
  WriteLooseCookedSceneWithScripting(cooked_root, scene_key);

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(cooked_root);

      // Desired behavior contract (currently failing): scene scripting
      // dependencies should not require source_pak when mounted from
      // loose-cooked sources.
      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

// P0.1.3: Source-aware script reload/invalidation characterization (YAML spec).
NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetReloadAllScriptsExpectedToNotifyForNonZeroSourceId)
{
  using namespace std::chrono_literals;

  const auto base_pak = GeneratePakFile("simple_material");
  const auto script_pak = GeneratePakFile("scene_with_scripting");
  const auto script_key
    = AssetKeyFromHex("11111111-1111-1111-1111-111111111111");

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadScriptAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadScriptResource);

    auto callback_called = std::make_shared<std::atomic<bool>>(false);
    auto callback_event = std::make_shared<oxygen::co::Event>();

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddPakFile(base_pak); // source_id=0
      loader.AddPakFile(script_pak); // source_id=1

      const auto script
        = co_await loader.LoadAssetAsync<ScriptAsset>(script_key);
      EXPECT_THAT(script, NotNull());
      if (!script) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      auto subscription = loader.SubscribeScriptReload(
        [callback_called, callback_event](const oxygen::data::AssetKey&,
          std::shared_ptr<const ScriptResource>) {
          const auto prior = callback_called->exchange(true);
          if (!prior) {
            callback_event->Trigger();
          }
        });
      (void)subscription;

      loader.ReloadAllScripts();

      auto timeout_task
        = pool.Run([](oxygen::co::ThreadPool::CancelToken token) {
            using namespace std::chrono_literals;
            auto remaining = 1500ms;
            while (!token.Peek() && remaining.count() > 0) {
              std::this_thread::sleep_for(10ms);
              remaining -= 10ms;
            }
            return !token.Peek();
          });
      auto [completed, timed_out]
        = co_await oxygen::co::AnyOf(*callback_event, std::move(timeout_task));

      // Desired behavior contract (currently failing): source-aware reload must
      // emit script reload callbacks for scripts loaded from source_id != 0.
      EXPECT_TRUE(completed.has_value());
      EXPECT_FALSE(timed_out.has_value() && timed_out.value());
      EXPECT_TRUE(callback_called->load());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

// P0.1.4: Refresh graph coherence characterization on loose refresh.
#if !defined(NDEBUG)
NOLINT_TEST_F(
  AssetLoaderScriptingTest, LoadAssetLooseRefreshExpectedToClearDependencyGraph)
{
  const auto cooked_root = temp_dir_ / "loose_refresh_graph";
  WriteMinimalLooseCookedIndex(cooked_root);
  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto dependent
    = AssetKeyFromHex("aaaaaaaa-0000-0000-0000-000000000001");
  const auto dependency
    = AssetKeyFromHex("bbbbbbbb-0000-0000-0000-000000000002");
  asset_loader_->AddAssetDependency(dependent, dependency);

  // Refresh same loose root. Current implementation clears cache/maps but does
  // not clear dependency graphs in this code path.
  asset_loader_->AddLooseCookedRoot(cooked_root);

  size_t dependent_count = 0;
  asset_loader_->ForEachDependent(
    dependency, [&](const oxygen::data::AssetKey&) { ++dependent_count; });

  // Desired behavior contract (currently failing): loose refresh should clear
  // dependency edges for replaced mount state.
  EXPECT_EQ(dependent_count, 0U);
}
#else
NOLINT_TEST_F(AssetLoaderScriptingTest,
  DISABLED_LoadAssetLooseRefreshExpectedToClearDependencyGraph)
{
  // Debug-only test: requires dependency graph introspection API that is not
  // part of release builds.
}
#endif

NOLINT_TEST_F(AssetLoaderScriptingTest,
  ContentSourceConformanceScriptCapabilitiesExpectedToMatch)
{
  const auto scene_key
    = AssetKeyFromHex("22222222-2222-2222-2222-222222222222");
  const auto pak_path = GeneratePakFile("scene_with_scripting");
  const auto cooked_root = temp_dir_ / "conformance_loose_scripting";
  WriteLooseCookedSceneWithScripting(cooked_root, scene_key);

  oxygen::content::internal::PakFileSource pak_source(pak_path, false);
  oxygen::content::internal::LooseCookedSource loose_source(cooked_root, false);

  auto assert_common = [&](oxygen::content::internal::IContentSource& source) {
    EXPECT_TRUE(source.HasAsset(scene_key));

    const auto header_opt = ReadAssetHeader(source, scene_key);
    ASSERT_TRUE(header_opt.has_value());
    EXPECT_EQ(static_cast<oxygen::data::AssetType>(header_opt->asset_type),
      oxygen::data::AssetType::kScene);

    EXPECT_THAT(source.CreateScriptTableReader(), NotNull());
    EXPECT_THAT(source.CreateScriptDataReader(), NotNull());
    EXPECT_THAT(source.GetScriptTable(), NotNull());
    EXPECT_GE(source.ScriptSlotCount(), 1U);

    const auto slots = source.ReadScriptSlotRecords(0, 1);
    ASSERT_EQ(slots.size(), 1U);
    const auto params = source.ReadScriptParamRecords(
      slots[0].params_array_offset, slots[0].params_count);
    EXPECT_EQ(params.size(), slots[0].params_count);

    const auto vpath = source.ResolveVirtualPath(scene_key);
    EXPECT_TRUE(vpath.has_value());
  };

  assert_common(pak_source);
  assert_common(loose_source);
}

// YAML integration baseline for scripting scene load path.
NOLINT_TEST_F(
  AssetLoaderScriptingTest, LoadAssetYamlSpecScriptingSceneFromPakLoads)
{
  const auto pak_path = GeneratePakFile("scene_with_scripting");
  const auto scene_key
    = AssetKeyFromHex("22222222-2222-2222-2222-222222222222");

  TestEventLoop el;
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadScriptAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadScriptResource);

    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddPakFile(pak_path);

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());
      if (scene) {
        const auto scripting = scene->GetComponents<
          oxygen::data::pak::scripting::ScriptingComponentRecord>();
        EXPECT_FALSE(scripting.empty());
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
