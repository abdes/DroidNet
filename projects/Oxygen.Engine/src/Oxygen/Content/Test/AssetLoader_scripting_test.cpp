//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "./AssetLoader_test.h"
#include "Fixtures/LooseCookedTestLayout.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::content::testing::LooseCookedLayout;
using oxygen::data::SceneAsset;
using oxygen::data::ScriptAsset;
using oxygen::data::ScriptResource;

namespace {

auto WriteLooseCookedScriptAsset(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& script_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::pak::scripting::ScriptAssetDesc;
  using oxygen::data::pak::scripting::ScriptResourceDesc;

  const LooseCookedLayout layout {};

  ScriptAssetDesc script_desc {};
  script_desc.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
  script_desc.header.version
    = oxygen::data::pak::scripting::kScriptAssetVersion;
  std::ranges::copy(
    std::string_view { "Reload" }, std::begin(script_desc.header.name));
  script_desc.bytecode_resource_index
    = oxygen::data::ResourceReferenceIndex { 0U };
  script_desc.source_resource_index = oxygen::data::kNoResourceReference;

  const auto desc_rel = std::filesystem::path("Scripts") / "Reload.oscript";

  constexpr std::array<uint8_t, 8> kBytecode
    = { 0x4D, 0x4F, 0x56, 0x45, 0x42, 0x43, 0xA1, 0xA2 };

  ScriptResourceDesc resource_desc {};
  resource_desc.data_offset = 0;
  resource_desc.size_bytes = static_cast<uint32_t>(kBytecode.size());
  resource_desc.content_hash = 0; // disable hash verification in this fixture

  oxygen::content::testing::LooseCookedTestWriter writer(cooked_root);
  writer.WriteAssetDescriptor(script_key, AssetType::kScript,
    std::string(layout.virtual_mount_root) + "/" + desc_rel.generic_string(),
    desc_rel.generic_string(), std::as_bytes(std::span { &script_desc, 1U }),
    oxygen::data::AssetReferences::Create(
      {
        {
          .kind = oxygen::data::ResourceKind::kScript,
          .index = oxygen::ResourceIndexT { 1U },
        },
      },
      {})
      .value());
  const auto resource_table
    = std::array { ScriptResourceDesc {}, resource_desc };
  writer.WriteFile(FileKind::kScriptsTable,
    std::string(layout.resources_dir) + "/scripts.table",
    std::as_bytes(std::span { resource_table }));
  writer.WriteFile(FileKind::kScriptsData,
    std::string(layout.resources_dir) + "/scripts.data",
    std::as_bytes(std::span { kBytecode }));
  static_cast<void>(writer.Finish());
}

auto WriteLooseCookedSceneWithScripting(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& scene_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::pak::scripting::ScriptingComponentRecord;
  using oxygen::data::pak::scripting::ScriptSlotRecord;
  using oxygen::data::pak::world::NodeRecord;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::data::pak::world::SceneComponentTableDesc;
  using oxygen::data::pak::world::SceneEnvironmentBlockHeader;

  const LooseCookedLayout layout {};

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  std::ranges::copy(
    std::string_view { "ScriptScene" }, std::begin(desc.header.name));
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const uint32_t offset_nodes = sizeof(SceneAssetDesc);
  const uint32_t offset_strings = offset_nodes + sizeof(NodeRecord);
  static constexpr auto kStrings = std::to_array("\0root\0");
  const uint32_t strings_size = sizeof(kStrings) - 1;
  const uint32_t offset_dir = offset_strings + strings_size;
  const uint32_t offset_table = offset_dir + sizeof(SceneComponentTableDesc);
  const uint32_t offset_slots = offset_table + sizeof(ScriptingComponentRecord);
  const uint32_t offset_env = offset_slots + sizeof(ScriptSlotRecord);
  desc.script_slots = {
    .offset = offset_slots,
    .count = 1U,
    .entry_size = sizeof(ScriptSlotRecord),
  };
  ScriptSlotRecord slot {};
  slot.script_asset_key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/scene-script.oscript");

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
  std::memcpy(
    std::span(bytes).subspan(offset_nodes).data(), &node, sizeof(node));
  std::memcpy(std::span(bytes).subspan(offset_strings).data(), kStrings.data(),
    strings_size);
  std::memcpy(std::span(bytes).subspan(offset_dir).data(), &component_desc,
    sizeof(component_desc));
  std::memcpy(std::span(bytes).subspan(offset_table).data(), &scripting,
    sizeof(scripting));
  std::memcpy(
    std::span(bytes).subspan(offset_slots).data(), &slot, sizeof(slot));
  std::memcpy(std::span(bytes).subspan(offset_env).data(), &env, sizeof(env));

  const auto rel_desc
    = std::filesystem::path(layout.scenes_subdir) / "ScriptScene.scene";

  oxygen::content::testing::LooseCookedTestWriter writer(cooked_root);
  writer.WriteAssetDescriptor(scene_key, AssetType::kScene,
    std::string(layout.virtual_mount_root) + "/" + rel_desc.generic_string(),
    rel_desc.generic_string(), bytes,
    oxygen::data::AssetReferences::Create({},
      {
        {
          .key = slot.script_asset_key,
          .kind = oxygen::data::KeyReferenceKind::kAsset,
          .expected_type = AssetType::kScript,
        },
      })
      .value());
  oxygen::data::pak::scripting::ScriptAssetDesc script {};
  script.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
  script.header.version = oxygen::data::pak::scripting::kScriptAssetVersion;
  writer.WriteAssetDescriptor(slot.script_asset_key, AssetType::kScript,
    "/Test/scene-script.oscript", "scene-script.oscript",
    std::as_bytes(std::span { &script, 1U }));
  writer.WriteFile(FileKind::kScriptsTable,
    std::string(layout.resources_dir) + "/scripts.table", {});
  writer.WriteFile(FileKind::kScriptsData,
    std::string(layout.resources_dir) + "/scripts.data", {});
  static_cast<void>(writer.Finish());
}

class AssetLoaderScriptingTest : public AssetLoaderBasicTest { };

NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetLooseCookedScriptResourceGenericLoadExpectedToSucceed)
{
  const auto script_key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Reload.oscript");
  const auto cooked_root = temp_dir_ / "loose_script";
  WriteLooseCookedScriptAsset(cooked_root, script_key);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey script_key, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
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
        if (!script_asset) {
          loader.Stop();
          co_return oxygen::co::kJoin;
        }

        const auto key = loader.MakeScriptResourceKeyForAsset(
          *script_asset, oxygen::data::ResourceReferenceIndex { 0U });
        EXPECT_TRUE(key.has_value());
        if (!key.has_value()) {
          loader.Stop();
          co_return oxygen::co::kJoin;
        }
        const auto script_resource
          = co_await loader.LoadResourceAsync<ScriptResource>(*key);

        EXPECT_THAT(script_resource, NotNull());
        if (script_resource) {
          EXPECT_THAT(script_resource->GetData(),
            ::testing::ElementsAre(
              0x4D, 0x4F, 0x56, 0x45, 0x42, 0x43, 0xA1, 0xA2));
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(script_key, cooked_root, &el));
}

NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetLooseCookedSceneWithScriptingExpectedToLoad)
{
  const auto scene_key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/Scripting.oscene");
  const auto cooked_root = temp_dir_ / "loose_scene_scripting";
  WriteLooseCookedSceneWithScripting(cooked_root, scene_key);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey scene_key, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
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

        const auto scene
          = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
        EXPECT_THAT(scene, NotNull());
        EXPECT_TRUE(
          loader.HasScriptAsset(oxygen::data::AssetKey::FromVirtualPath(
            "/Test/scene-script.oscript")));

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(scene_key, cooked_root, &el));
}

} // namespace
