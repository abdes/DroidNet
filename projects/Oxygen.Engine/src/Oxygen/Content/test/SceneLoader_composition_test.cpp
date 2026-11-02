//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
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

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneRefusal;
using oxygen::data::CookedDefect;

constexpr world::SceneNodeIndexT kNodeCount = 3U;
constexpr world::SceneNodeIndexT kCameraNode = 2U;

//! A starting input context and its priority.
struct StartingContext {
  AssetKey key;
  int32_t priority = 0;
};

//! Scene-level values of a three-node scene whose node 2 owns a camera.
struct CompositionInput {
  bool model_scene = false;
  bool starting_camera = false;
  //! The nodes that instance the model scene.
  std::vector<world::SceneNodeIndexT> instances;
  std::vector<StartingContext> contexts;
};

auto MakeComposedScene(const CompositionInput& input) -> std::vector<std::byte>
{
  auto scene = SceneBuilder { "test" };
  auto nodes = std::vector<oxygen::cooker::testing::SceneNode> {};
  for (world::SceneNodeIndexT index = 0; index < kNodeCount; ++index) {
    nodes.push_back(scene.Node("node" + std::to_string(index)));
  }
  scene.PerspectiveCamera(nodes.at(kCameraNode));
  for (const auto node : input.instances) {
    scene.Instance(nodes.at(node), ModelKey());
  }
  for (const auto& context : input.contexts) {
    scene.StartingInputContext(context.key, context.priority);
  }
  if (input.starting_camera) {
    scene.StartingCamera(nodes.at(kCameraNode));
  }
  if (input.model_scene) {
    scene.ModelScene();
  }
  return scene.Bytes();
}

auto LoadParseOnly(std::vector<std::byte>& bytes)
  -> std::unique_ptr<oxygen::data::SceneAsset>
{
  auto stream
    = oxygen::serio::ReadOnlyMemoryStream(std::span<const std::byte>(bytes));
  auto reader = oxygen::serio::Reader(stream);
  auto context = oxygen::content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;
  return LoadSceneAsset(context);
}

//! The loader keeps the scene flags, starting camera, instances and the
//! starting contexts in their authored order.
NOLINT_TEST(SceneCompositionLoadTest, PreservesInstancesAndStartState)
{
  constexpr int32_t kMenuPriority = -4;
  auto input = CompositionInput {};
  input.starting_camera = true;
  input.instances = { 1U };
  input.contexts = {
    { .key = MenuKey(), .priority = kMenuPriority },
    { .key = GameplayKey(), .priority = 1 },
  };
  auto bytes = MakeComposedScene(input);

  const auto scene = LoadParseOnly(bytes);

  ASSERT_NE(scene, nullptr);
  EXPECT_FALSE(scene->IsModelScene());
  EXPECT_EQ(scene->GetStartingCamera(),
    std::optional { world::NodeRef { .node_index = kCameraNode } });
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
  input.model_scene = true;
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
  input.instances = { 1U };
  input.contexts = { { .key = GameplayKey(), .priority = 0 } };
  auto bytes = MakeComposedScene(input);
  auto stream
    = oxygen::serio::ReadOnlyMemoryStream(std::span<const std::byte>(bytes));
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

//! A valid scene with one defect, and the defect it is refused with.
struct InvalidComposition final {
  std::vector<std::byte> bytes;
  CookedDefect defect;
};

auto InvalidCompositions() -> std::vector<InvalidComposition>
{
  auto cases = std::vector<InvalidComposition> {};

  const auto camera_defect = [&cases](const world::SceneNodeIndexT node) {
    auto bytes = MakeComposedScene({ .starting_camera = true });
    DescriptorDefects::SetStartingCamera(
      bytes, world::NodeRef { .node_index = node });
    cases.push_back(
      { .bytes = std::move(bytes), .defect = CookedDefect::kInvalidReference });
  };
  camera_defect(0U); // Not a camera node.
  camera_defect(kNodeCount); // Not a node.

  const auto instance_defect =
    [&cases](std::vector<world::SceneNodeIndexT> nodes, const uint32_t index,
      const CookedDefect defect, const auto& edit) {
      auto bytes = MakeComposedScene({ .instances = std::move(nodes) });
      const auto first
        = DescriptorDefects::ComponentAt<world::SceneInstanceRecord>(
          bytes, ComponentType::kSceneInstance, 0U);
      auto record = DescriptorDefects::ComponentAt<world::SceneInstanceRecord>(
        bytes, ComponentType::kSceneInstance, index);
      edit(record, first);
      DescriptorDefects::SetComponent(
        bytes, ComponentType::kSceneInstance, index, record);
      cases.push_back({ .bytes = std::move(bytes), .defect = defect });
    };
  instance_defect({ 1U }, 0U, CookedDefect::kInvalidReference,
    [](auto& record, const auto&) { record.node_index = kNodeCount; });
  instance_defect({ 1U }, 0U, CookedDefect::kMissingPart,
    [](auto& record, const auto&) { record.scene_key = {}; });
  instance_defect({ 0U, 1U }, 1U, CookedDefect::kDuplicate,
    [](auto& record, const auto& first) {
      record.node_index = first.node_index;
    });

  const auto context_defect
    = [&cases](const CompositionInput& input, const uint32_t index,
        const CookedDefect defect, const auto& key_of) {
        auto bytes = MakeComposedScene(input);
        auto record
          = DescriptorDefects::ComponentAt<world::StartingInputContextRecord>(
            bytes, ComponentType::kStartingInputContext, index);
        record.context_key = key_of(bytes);
        DescriptorDefects::SetComponent(
          bytes, ComponentType::kStartingInputContext, index, record);
        cases.push_back({ .bytes = std::move(bytes), .defect = defect });
      };
  context_defect({ .contexts = { { .key = MenuKey() } } }, 0U,
    CookedDefect::kMissingPart,
    [](const std::vector<std::byte>&) { return AssetKey {}; });
  context_defect(
    {
      .contexts
      = { { .key = MenuKey() }, { .key = GameplayKey(), .priority = 1 } },
    },
    1U, CookedDefect::kDuplicate, [](const std::vector<std::byte>& bytes) {
      return DescriptorDefects::ComponentAt<world::StartingInputContextRecord>(
        bytes, ComponentType::kStartingInputContext, 0U)
        .context_key;
    });

  auto flagged = MakeComposedScene({});
  DescriptorDefects::SetSceneFlags(flagged, 1U << 1U);
  cases.push_back(
    { .bytes = std::move(flagged), .defect = CookedDefect::kInvalidValue });
  return cases;
}

//! Invalid start state or instances fail both the asset and the loader.
NOLINT_TEST(SceneCompositionLoadTest, RejectsInvalidCompositionRecords)
{
  auto case_index = size_t { 0 };
  for (const auto& [bytes, defect] : InvalidCompositions()) {
    SCOPED_TRACE(case_index++);
    EXPECT_EQ(SceneRefusal(bytes), defect);
  }
}

} // namespace
