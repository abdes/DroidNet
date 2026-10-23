//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace world = oxygen::data::pak::world;
using oxygen::base::CheckedAt;
using oxygen::content::loaders::LoadSceneAsset;
using oxygen::data::AssetKey;
using oxygen::data::ComponentType;

//! Scene-level values of a three-node scene whose node 2 owns a camera.
struct CompositionInput {
  world::SceneAssetFlags flags = world::SceneAssetFlags::kNone;
  world::SceneNodeIndexT starting_camera = world::kNoStartingCamera;
  std::vector<world::SceneInstanceRecord> instances;
  std::vector<world::StartingInputContextRecord> contexts;
};

constexpr world::SceneNodeIndexT kNodeCount = 3U;
constexpr world::SceneNodeIndexT kCameraNode = 2U;

template <typename T>
auto Append(std::vector<std::byte>& bytes, const T& value) -> void
{
  const auto view = std::as_bytes(std::span<const T, 1>(&value, 1));
  bytes.insert(bytes.end(), view.begin(), view.end());
}

template <typename T>
auto AppendTable(std::vector<std::byte>& bytes, const std::vector<T>& records)
  -> void
{
  const auto view = std::as_bytes(std::span(records));
  bytes.insert(bytes.end(), view.begin(), view.end());
}

auto MakeComposedScene(const CompositionInput& input) -> std::vector<std::byte>
{
  auto camera = world::MakeDefaultRecord<world::PerspectiveCameraRecord>();
  camera.node_index = kCameraNode;
  const auto cameras = std::vector { camera };

  auto tables = std::vector<world::SceneComponentTableDesc> {};
  const auto add_table
    = [&tables](const ComponentType type, const size_t entry_size,
        const size_t count) -> void {
    if (count != 0U) {
      tables.push_back({
        .component_type = static_cast<uint32_t>(type),
        .table = { .offset = 0U,
          .count = static_cast<uint32_t>(count),
          .entry_size = static_cast<uint32_t>(entry_size), },
      });
    }
  };
  add_table(ComponentType::kPerspectiveCamera,
    sizeof(world::PerspectiveCameraRecord), cameras.size());
  add_table(ComponentType::kSceneInstance, sizeof(world::SceneInstanceRecord),
    input.instances.size());
  add_table(ComponentType::kStartingInputContext,
    sizeof(world::StartingInputContextRecord), input.contexts.size());

  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  descriptor.nodes = {
    .offset = sizeof(world::SceneAssetDesc),
    .count = kNodeCount,
    .entry_size = sizeof(world::NodeRecord),
  };
  descriptor.flags = input.flags;
  descriptor.starting_camera = input.starting_camera;
  auto offset
    = descriptor.nodes.offset + (kNodeCount * sizeof(world::NodeRecord));
  const auto strings = oxygen::data::DescriptorStringsBuilder {};
  descriptor.header.strings = {
    .offset = static_cast<oxygen::data::pak::core::StringTableOffsetT>(offset),
    .size = strings.Size(),
  };
  offset += strings.Size();
  descriptor.component_table_directory_offset = offset;
  descriptor.component_table_count = static_cast<uint32_t>(tables.size());
  offset += tables.size() * sizeof(world::SceneComponentTableDesc);
  for (auto& table : tables) {
    table.table.offset = offset;
    offset += size_t { table.table.count } * table.table.entry_size;
  }

  auto bytes = std::vector<std::byte> {};
  Append(bytes, descriptor);
  for (world::SceneNodeIndexT index = 0; index < kNodeCount; ++index) {
    auto node = world::NodeRecord {};
    node.parent_index = 0U;
    Append(bytes, node);
  }
  const auto table_bytes = strings.Bytes();
  bytes.insert(bytes.end(), table_bytes.begin(), table_bytes.end());
  AppendTable(bytes, tables);
  AppendTable(bytes, cameras);
  AppendTable(bytes, input.instances);
  AppendTable(bytes, input.contexts);
  auto environment = world::SceneEnvironmentBlockHeader {};
  environment.byte_size = sizeof(environment);
  Append(bytes, environment);
  return bytes;
}

auto LoadParseOnly(std::vector<std::byte>& bytes)
  -> std::unique_ptr<oxygen::data::SceneAsset>
{
  auto stream = oxygen::serio::MemoryStream(std::span<std::byte>(bytes));
  auto reader = oxygen::serio::Reader(stream);
  auto context = oxygen::content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  return LoadSceneAsset(context);
}

auto ModelKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/Scenes/Model.oscene");
}

auto GameplayKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/Input/Gameplay.oimap");
}

auto MenuKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/Input/Menu.oimap");
}

//! The loader keeps the scene flags, starting camera, instances and the
//! starting contexts in their authored order.
NOLINT_TEST(SceneCompositionLoadTest, PreservesInstancesAndStartState)
{
  constexpr int32_t kMenuPriority = -4;
  auto input = CompositionInput {};
  input.starting_camera = kCameraNode;
  input.instances = { { .node_index = 1U, .scene_key = ModelKey() } };
  input.contexts = {
    { .context_key = MenuKey(), .priority = kMenuPriority },
    { .context_key = GameplayKey(), .priority = 1 },
  };
  auto bytes = MakeComposedScene(input);

  const auto scene = LoadParseOnly(bytes);

  ASSERT_NE(scene, nullptr);
  EXPECT_FALSE(scene->IsModelScene());
  EXPECT_EQ(scene->GetStartingCamera(),
    std::optional<world::SceneNodeIndexT> { kCameraNode });
  const auto instances = scene->GetComponents<world::SceneInstanceRecord>();
  ASSERT_EQ(instances.size(), 1U);
  EXPECT_EQ(CheckedAt(instances, 0).node_index, 1U);
  EXPECT_EQ(CheckedAt(instances, 0).scene_key, ModelKey());
  const auto contexts
    = scene->GetComponents<world::StartingInputContextRecord>();
  ASSERT_EQ(contexts.size(), 2U);
  EXPECT_EQ(CheckedAt(contexts, 0).context_key, MenuKey());
  EXPECT_EQ(CheckedAt(contexts, 0).priority, kMenuPriority);
  EXPECT_EQ(CheckedAt(contexts, 1).context_key, GameplayKey());
}

//! A model scene says so and has no start state.
NOLINT_TEST(SceneCompositionLoadTest, ReportsModelScene)
{
  auto input = CompositionInput {};
  input.flags = world::SceneAssetFlags::kModelScene;
  auto bytes = MakeComposedScene(input);

  const auto scene = LoadParseOnly(bytes);

  ASSERT_NE(scene, nullptr);
  EXPECT_TRUE(scene->IsModelScene());
  EXPECT_FALSE(scene->GetStartingCamera().has_value());
}

//! Instanced scenes and starting contexts are loaded by instantiation, so a
//! missing one cannot fail the scene's own load.
NOLINT_TEST(
  SceneCompositionLoadTest, InstancesAndContextsAreNotLoadDependencies)
{
  auto input = CompositionInput {};
  input.instances = { { .node_index = 1U, .scene_key = ModelKey() } };
  input.contexts = { { .context_key = GameplayKey(), .priority = 0 } };
  auto bytes = MakeComposedScene(input);
  auto stream = oxygen::serio::MemoryStream(std::span<std::byte>(bytes));
  auto reader = oxygen::serio::Reader(stream);
  auto collector
    = std::make_shared<oxygen::content::internal::DependencyCollector>();
  const auto context = oxygen::content::LoaderContext {
    .current_asset_key = AssetKey {},
    .source_instance = oxygen::data::SourceInstanceId(1U),
    .desc_reader = &reader,
    .work_offline = true,
    .dependency_collector = collector,
    .parse_only = false,
  };

  ASSERT_NE(LoadSceneAsset(context), nullptr);

  EXPECT_TRUE(collector->AssetDependencies().empty());
}

//! One invalid composition per case, each otherwise valid.
auto InvalidCompositions() -> std::vector<CompositionInput>
{
  auto inputs = std::vector<CompositionInput> {};
  const auto add
    = [&inputs] -> CompositionInput& { return inputs.emplace_back(); };
  add().starting_camera = 0U; // Not a camera node.
  add().starting_camera = kNodeCount; // Not a node.
  add().instances = { { .node_index = kNodeCount, .scene_key = ModelKey() } };
  add().instances = { { .node_index = 1U, .scene_key = {} } };
  add().instances = {
    { .node_index = 1U, .scene_key = ModelKey() },
    { .node_index = 1U, .scene_key = ModelKey() },
  };
  add().contexts = { { .context_key = {}, .priority = 0 } };
  add().contexts = {
    { .context_key = MenuKey(), .priority = 0 },
    { .context_key = MenuKey(), .priority = 1 },
  };
  add().flags = static_cast<world::SceneAssetFlags>(1U << 1U);
  return inputs;
}

//! Invalid start state or instances fail both the asset and the loader.
NOLINT_TEST(SceneCompositionLoadTest, RejectsInvalidCompositionRecords)
{
  auto case_index = size_t { 0 };
  for (const auto& input : InvalidCompositions()) {
    SCOPED_TRACE(case_index++);
    auto bytes = MakeComposedScene(input);
    EXPECT_THROW(
      (oxygen::data::SceneAsset(AssetKey {}, bytes)), std::runtime_error);
    EXPECT_THROW(LoadParseOnly(bytes), std::runtime_error);
  }
}

} // namespace
