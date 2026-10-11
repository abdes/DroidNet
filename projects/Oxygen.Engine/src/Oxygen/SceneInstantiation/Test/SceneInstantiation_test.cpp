//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Test/Fakes/StubAssetLoader.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::instantiation::test {

namespace {
  namespace world = data::pak::world;
  using data::AssetKey;
  using data::ComponentType;
  using data::testing::SceneAssetBuilder;

  constexpr auto kSceneCapacity = 64U;
  constexpr int32_t kGameplayPriority = 10;
  constexpr int32_t kMenuPriority = 20;

  auto Key(const std::string_view path) -> AssetKey
  {
    return AssetKey::FromVirtualPath(path);
  }

  auto Camera(const uint32_t node) -> world::PerspectiveCameraRecord
  {
    auto record = world::PerspectiveCameraRecord {};
    record.node_index = node;
    return record;
  }

  auto MakeContext(const AssetKey& key, const std::string_view name)
    -> std::shared_ptr<data::InputMappingContextAsset>
  {
    auto desc = data::pak::input::InputMappingContextAssetDesc {};
    std::ranges::copy(name, std::begin(desc.header.name));
    return std::make_shared<data::InputMappingContextAsset>(key, desc);
  }

  //! Serves the scenes and input contexts a test registers.
  class TestLoader final : public content::testing::StubAssetLoader {
  public:
    using StubAssetLoader::StartLoadScene;

    auto AddScene(const AssetKey& key, std::shared_ptr<data::SceneAsset> scene)
      -> void
    {
      scenes_.insert_or_assign(key, std::move(scene));
    }

    auto AddContext(const AssetKey& key, const std::string_view name) -> void
    {
      contexts_.insert_or_assign(key, MakeContext(key, name));
    }

    void StartLoadScene(const AssetKey& key, SceneCallback on_complete) override
    {
      const auto it = scenes_.find(key);
      on_complete(it != scenes_.end() ? it->second : nullptr);
    }

    void StartLoadInputMappingContextAsset(
      const AssetKey& key, InputMappingContextCallback on_complete) override
    {
      const auto it = contexts_.find(key);
      on_complete(it != contexts_.end() ? it->second : nullptr);
    }

  private:
    std::unordered_map<AssetKey, std::shared_ptr<data::SceneAsset>> scenes_;
    std::unordered_map<AssetKey,
      std::shared_ptr<data::InputMappingContextAsset>>
      contexts_;
  };

  class SceneInstantiationTest : public ::testing::Test {
  protected:
    auto Run(std::shared_ptr<const data::SceneAsset> asset) -> SceneInstance
    {
      co::testing::TestEventLoop loop;
      return co::Run(loop,
        Instantiate(
          SceneInstantiationRequest {
            .asset = std::move(asset),
            .target = observer_ptr { scene_.get() },
            .scripts = nullptr,
            .input = observer_ptr { &input_ },
          },
          loader_));
    }

    [[nodiscard]] auto Scene() -> scene::Scene& { return *scene_; }
    [[nodiscard]] auto Loader() -> TestLoader& { return loader_; }
    [[nodiscard]] auto Input() -> engine::InputSystem& { return input_; }

  private:
    std::shared_ptr<scene::Scene> scene_
      = std::make_shared<scene::Scene>("instantiate", kSceneCapacity);
    TestLoader loader_;
    co::BroadcastChannel<platform::InputEvent> input_channel_;
    engine::InputSystem input_ { input_channel_.ForRead() };
  };

  auto HasIssue(const SceneInstance& instance, const std::string_view text)
    -> bool
  {
    return std::ranges::any_of(
      instance.issues, [text](const SceneInstantiationIssue& issue) -> bool {
        return issue.message.contains(text);
      });
  }

} // namespace

//! Nodes are created in index order with their names, hierarchy and local
//! transforms.
NOLINT_TEST_F(SceneInstantiationTest, CreatesNodesWithHierarchyAndTransforms)
{
  auto builder = SceneAssetBuilder {};
  const auto root = builder.AddNode("Root");
  const auto child = builder.AddNode("Child", root);
  builder.Node(child).translation[1] = 2.0F;
  builder.AddNode("");

  const auto instance = Run(builder.Build());

  ASSERT_EQ(instance.nodes.size(), 3U);
  EXPECT_EQ(instance.nodes.at(0).GetName(), "Root");
  EXPECT_EQ(instance.nodes.at(2).GetName(), "Node2");
  auto child_node = instance.nodes.at(1);
  ASSERT_TRUE(child_node.GetParent().has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(child_node.GetParent().value().GetName(), "Root");
  const auto position = child_node.GetTransform().GetLocalPosition();
  ASSERT_TRUE(position.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(position.value().y, 2.0F);
}

//! The authored starting camera becomes the active camera.
NOLINT_TEST_F(SceneInstantiationTest, StartingCameraBecomesActive)
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode("First");
  builder.AddNode("Second");
  builder.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { Camera(0U), Camera(1U) });
  builder.SetStartingCamera(1U);

  const auto instance = Run(builder.Build());

  const auto active = Scene().ActiveCamera();
  ASSERT_TRUE(active.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(active.value().GetName(), "Second");
  EXPECT_TRUE(instance.issues.empty());
}

//! Without an authored starting camera the first camera is used, with an
//! issue saying so.
NOLINT_TEST_F(SceneInstantiationTest, FirstCameraIsUsedWhenNoneIsAuthored)
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode("Plain");
  builder.AddNode("Eye");
  builder.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { Camera(1U) });

  const auto instance = Run(builder.Build());

  const auto active = Scene().ActiveCamera();
  ASSERT_TRUE(active.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(active.value().GetName(), "Eye");
  EXPECT_TRUE(HasIssue(instance, "no starting camera is authored"));
}

//! An instanced scene's nodes are created under the instance node; a missing
//! instanced scene is an issue and the rest of the scene loads.
NOLINT_TEST_F(SceneInstantiationTest, InstancedScenesLoadUnderTheirNodes)
{
  const auto model_key = Key("/Test/Model.oscene");
  auto model = SceneAssetBuilder {};
  const auto model_root = model.AddNode("ModelRoot");
  model.AddNode("Wheel", model_root);
  model.SetFlags(world::SceneAssetFlags::kModelScene);
  Loader().AddScene(model_key, model.Build(model_key));

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Holder");
  builder.AddNode("Orphan");
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord { .node_index = 0U, .scene_key = model_key },
      world::SceneInstanceRecord {
        .node_index = 1U,
        .scene_key = Key("/Test/Missing.oscene"),
      },
    });

  const auto instance = Run(builder.Build());

  auto holder = instance.nodes.at(0);
  auto child = holder.GetFirstChild();
  ASSERT_TRUE(child.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(child.value().GetName(), "ModelRoot");
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  auto wheel = child.value().GetFirstChild();
  ASSERT_TRUE(wheel.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(wheel.value().GetName(), "Wheel");
  EXPECT_TRUE(HasIssue(instance, "is not available"));
}

//! Starting input contexts are added with their priority and activated; one
//! that fails to load is an issue and the others still start.
NOLINT_TEST_F(SceneInstantiationTest, StartingInputContextsStart)
{
  const auto gameplay = Key("/Test/Gameplay.oimap");
  const auto menu = Key("/Test/Menu.oimap");
  Loader().AddContext(gameplay, "Gameplay");
  Loader().AddContext(menu, "Menu");

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Root");
  builder.AddComponents(ComponentType::kStartingInputContext,
    std::vector {
      world::StartingInputContextRecord {
        .context_key = gameplay,
        .priority = kGameplayPriority,
      },
      world::StartingInputContextRecord {
        .context_key = Key("/Test/Missing.oimap"),
        .priority = 0,
      },
      world::StartingInputContextRecord {
        .context_key = menu,
        .priority = kMenuPriority,
      },
    });

  const auto instance = Run(builder.Build());

  EXPECT_NE(Input().GetMappingContextByKey(gameplay), nullptr);
  EXPECT_NE(Input().GetMappingContextByKey(menu), nullptr);
  EXPECT_TRUE(HasIssue(instance, "is not available"));
}

//! A renderable whose geometry is not loaded is an issue on its node.
NOLINT_TEST_F(SceneInstantiationTest, MissingGeometryIsAnIssue)
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode("Mesh");
  auto renderable = world::RenderableRecord {};
  renderable.node_index = 0U;
  renderable.geometry_key = Key("/Test/Mesh.ogeo");
  renderable.visible = 1U;
  builder.AddComponents(ComponentType::kRenderable, std::vector { renderable });

  const auto instance = Run(builder.Build());

  ASSERT_EQ(instance.nodes.size(), 1U);
  ASSERT_FALSE(instance.issues.empty());
  EXPECT_EQ(instance.issues.front().node_index, 0U);
  EXPECT_TRUE(HasIssue(instance, "geometry"));
}

} // namespace oxygen::instantiation::test
