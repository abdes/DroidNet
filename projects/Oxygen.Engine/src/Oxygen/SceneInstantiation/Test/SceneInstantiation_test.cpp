//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
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
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
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
  constexpr float kFieldOfView = 1.1F;
  constexpr float kOrthoHalfWidth = 4.0F;
  constexpr float kSunLux = 90000.0F;
  constexpr float kBulbFlux = 1600.0F;
  constexpr float kSpotOuterCone = 0.7F;
  constexpr int32_t kFogSortPriority = 3;
  constexpr float kFogExtinction = 0.05F;

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

  //! A one-triangle geometry whose only submesh uses `slot`.
  auto MakeGeometry(const AssetKey& key, const data::MaterialSlotId slot)
    -> std::shared_ptr<data::GeometryAsset>
  {
    constexpr uint32_t kTriangleSize = 3U;
    auto vertices = std::vector<data::Vertex>(kTriangleSize);
    vertices.at(1).position = { 1.0F, 0.0F, 0.0F };
    vertices.at(2).position = { 0.0F, 1.0F, 0.0F };
    constexpr auto kIndices
      = std::array<uint32_t, kTriangleSize> { 0U, 1U, 2U };
    auto builder = data::MeshBuilder(data::LodIndex { 0U }, "mesh");
    builder.WithVertices(std::move(vertices)).WithIndices(kIndices);
    builder.BeginSubMesh("submesh", data::MaterialAsset::CreateDefault())
      .WithMaterialSlotId(slot)
      .WithMeshView({
        .first_index = 0U,
        .index_count = kTriangleSize,
        .first_vertex = 0U,
        .vertex_count = kTriangleSize,
      })
      .EndSubMesh();
    auto meshes = std::vector<std::shared_ptr<data::Mesh>> {};
    meshes.push_back(builder.Build());
    auto desc = data::pak::geometry::GeometryAssetDesc {};
    desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    desc.lod_count = 1U;
    return std::make_shared<data::GeometryAsset>(key, desc, std::move(meshes));
  }

  //! Binds `slot` to `action` in `context` with an explicit Pressed trigger.
  auto Bind(input::InputMappingContext& context,
    const std::shared_ptr<input::Action>& action,
    const platform::InputSlot& slot) -> void
  {
    auto mapping = std::make_shared<input::InputActionMapping>(action, slot);
    auto trigger = std::make_shared<input::ActionTriggerPressed>();
    trigger->MakeExplicit();
    mapping->AddTrigger(trigger);
    context.AddMapping(mapping);
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

    auto AddGeometry(std::shared_ptr<data::GeometryAsset> geometry) -> void
    {
      const auto key = geometry->GetAssetKey();
      geometries_.insert_or_assign(key, std::move(geometry));
    }

    auto AddMaterial(const AssetKey& key)
      -> std::shared_ptr<const data::MaterialAsset>
    {
      auto material = std::make_shared<data::MaterialAsset>(
        key, data::pak::render::MaterialAssetDesc {});
      materials_.insert_or_assign(key, material);
      return material;
    }

    auto GetGeometryAsset(const AssetKey& key, const data::Asset& /*context*/)
      -> std::shared_ptr<data::GeometryAsset> override
    {
      const auto it = geometries_.find(key);
      return it != geometries_.end() ? it->second : nullptr;
    }

    auto GetMaterialAsset(const AssetKey& key, const data::Asset& /*context*/)
      -> std::shared_ptr<data::MaterialAsset> override
    {
      const auto it = materials_.find(key);
      return it != materials_.end() ? it->second : nullptr;
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
    std::unordered_map<AssetKey, std::shared_ptr<data::GeometryAsset>>
      geometries_;
    std::unordered_map<AssetKey, std::shared_ptr<data::MaterialAsset>>
      materials_;
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

    //! Presses Space and runs the input phases of one frame.
    auto PressSpace() -> void
    {
      const auto event = std::make_shared<platform::KeyEvent>(
        time::PhysicalTime { std::chrono::steady_clock::now() },
        platform::kInvalidWindowId,
        platform::input::KeyInfo(platform::Key::kSpace, false),
        platform::ButtonState::kPressed);
      ASSERT_TRUE(input_channel_.ForWrite().TrySend(event));
      auto frame = engine::FrameContext {};
      co::testing::TestEventLoop loop;
      co::Run(loop, [this, &frame] -> co::Co<> {
        input_.OnFrameStart(observer_ptr { &frame });
        co_await input_.OnInput(observer_ptr { &frame });
      });
    }

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

//! Renderables with their material overrides, both camera kinds, every light
//! kind and local fog volumes are attached with their authored values.
NOLINT_TEST_F(SceneInstantiationTest, AttachesEveryNodeRecordFamily)
{
  const auto slot = data::MaterialSlotId::FromStableIdentity("test/slot");
  const auto geometry = MakeGeometry(Key("/Test/Mesh.ogeo"), slot);
  const auto material_key = Key("/Test/Override.omat");
  Loader().AddGeometry(geometry);
  const auto material = Loader().AddMaterial(material_key);

  auto builder = SceneAssetBuilder {};
  const auto mesh = builder.AddNode("Mesh");
  const auto eye = builder.AddNode("Eye");
  const auto map = builder.AddNode("Map");
  const auto sun = builder.AddNode("Sun");
  const auto bulb = builder.AddNode("Bulb");
  const auto spot = builder.AddNode("Spot");
  const auto haze = builder.AddNode("Haze");
  auto renderable = world::RenderableRecord {};
  renderable.node_index = mesh;
  renderable.geometry_key = geometry->GetAssetKey();
  builder.AddComponents(ComponentType::kRenderable, std::vector { renderable });
  builder.AddComponents(ComponentType::kMaterialOverride,
    std::vector {
      world::MaterialOverrideRecord {
        .node_index = mesh,
        .slot_id = slot,
        .material_key = material_key,
        .layout_revision = geometry->MaterialSlots().layout_revision,
      },
    });
  auto perspective = Camera(eye);
  perspective.fov_y = kFieldOfView;
  builder.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { perspective });
  auto orthographic = world::OrthographicCameraRecord {};
  orthographic.node_index = map;
  orthographic.left = -kOrthoHalfWidth;
  orthographic.right = kOrthoHalfWidth;
  builder.AddComponents(
    ComponentType::kOrthographicCamera, std::vector { orthographic });
  auto directional = world::DirectionalLightRecord {};
  directional.node_index = sun;
  directional.intensity_lux = kSunLux;
  builder.AddComponents(
    ComponentType::kDirectionalLight, std::vector { directional });
  auto point = world::PointLightRecord {};
  point.node_index = bulb;
  point.luminous_flux_lm = kBulbFlux;
  builder.AddComponents(ComponentType::kPointLight, std::vector { point });
  auto spot_light = world::SpotLightRecord {};
  spot_light.node_index = spot;
  spot_light.outer_cone_angle_radians = kSpotOuterCone;
  builder.AddComponents(ComponentType::kSpotLight, std::vector { spot_light });
  auto fog = world::LocalFogVolumeRecord {};
  fog.node_index = haze;
  fog.sort_priority = kFogSortPriority;
  builder.AddComponents(ComponentType::kLocalFogVolume, std::vector { fog });
  builder.SetStartingCamera(eye);

  auto instance = Run(builder.Build());

  EXPECT_TRUE(instance.issues.empty());
  ASSERT_EQ(instance.nodes.size(), 7U);
  const auto mesh_renderable = instance.nodes.at(mesh).GetRenderable();
  EXPECT_EQ(mesh_renderable.GetGeometry(), geometry);
  EXPECT_EQ(mesh_renderable.ResolveSubmeshMaterial(
              data::LodIndex { 0U }, data::SubmeshIndex { 0U }),
    material);
  const auto eye_camera
    = instance.nodes.at(eye).GetCameraAs<scene::PerspectiveCamera>();
  ASSERT_TRUE(eye_camera.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(eye_camera->get().GetFieldOfView(), kFieldOfView);
  const auto map_camera
    = instance.nodes.at(map).GetCameraAs<scene::OrthographicCamera>();
  ASSERT_TRUE(map_camera.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(map_camera->get().GetExtents().at(1), kOrthoHalfWidth);
  const auto sun_light
    = instance.nodes.at(sun).GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(sun_light.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(sun_light->get().GetIntensityLux(), kSunLux);
  const auto bulb_light
    = instance.nodes.at(bulb).GetLightAs<scene::PointLight>();
  ASSERT_TRUE(bulb_light.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(bulb_light->get().GetLuminousFluxLm(), kBulbFlux);
  const auto spot_component
    = instance.nodes.at(spot).GetLightAs<scene::SpotLight>();
  ASSERT_TRUE(spot_component.has_value());
  EXPECT_FLOAT_EQ(
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
    spot_component->get().GetOuterConeAngleRadians(), kSpotOuterCone);
  const auto haze_impl = instance.nodes.at(haze).GetImpl();
  ASSERT_TRUE(haze_impl.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  const auto& haze_node = haze_impl->get();
  ASSERT_TRUE(haze_node.HasComponent<scene::environment::LocalFogVolume>());
  EXPECT_EQ(haze_node.GetComponent<scene::environment::LocalFogVolume>()
              .GetSortPriority(),
    kFogSortPriority);
}

//! The scene's environment records become the target scene's environment.
NOLINT_TEST_F(SceneInstantiationTest, AppliesTheEnvironment)
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode("Root");
  auto fog = world::FogEnvironmentRecord {};
  fog.extinction_sigma_t_per_m = kFogExtinction;
  builder.AddEnvironment(fog);

  static_cast<void>(Run(builder.Build()));

  const auto environment = Scene().GetEnvironment();
  ASSERT_NE(environment, nullptr);
  const auto scene_fog = environment->TryGetSystem<scene::environment::Fog>();
  ASSERT_NE(scene_fog, nullptr);
  EXPECT_FLOAT_EQ(scene_fog->GetExtinctionSigmaTPerMeter(), kFogExtinction);
}

//! An instanced scene contributes its nodes, never its environment or start
//! state.
NOLINT_TEST_F(SceneInstantiationTest, InstancedScenesContributeNoStartState)
{
  const auto model_key = Key("/Test/Model.oscene");
  const auto model_context = Key("/Test/Model.oimap");
  Loader().AddContext(model_context, "Model");
  auto model = SceneAssetBuilder {};
  model.AddNode("ModelEye");
  model.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { Camera(0U) });
  model.SetStartingCamera(0U);
  model.AddComponents(ComponentType::kStartingInputContext,
    std::vector {
      world::StartingInputContextRecord {
        .context_key = model_context,
        .priority = 0,
      },
    });
  model.AddEnvironment(world::FogEnvironmentRecord {});
  model.SetFlags(world::SceneAssetFlags::kModelScene);
  Loader().AddScene(model_key, model.Build(model_key));

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Eye");
  builder.AddNode("Holder");
  builder.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { Camera(0U) });
  builder.SetStartingCamera(0U);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord { .node_index = 1U, .scene_key = model_key },
    });

  const auto instance = Run(builder.Build());

  EXPECT_TRUE(instance.issues.empty());
  const auto active = Scene().ActiveCamera();
  ASSERT_TRUE(active.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(active.value().GetName(), "Eye");
  EXPECT_EQ(Input().GetMappingContextByKey(model_context), nullptr);
  const auto environment = Scene().GetEnvironment();
  EXPECT_TRUE(environment == nullptr
    || environment->TryGetSystem<scene::environment::Fog>() == nullptr);
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

  EXPECT_TRUE(HasIssue(instance, "is not available"));
  const auto gameplay_context = Input().GetMappingContextByKey(gameplay);
  const auto menu_context = Input().GetMappingContextByKey(menu);
  ASSERT_NE(gameplay_context, nullptr);
  ASSERT_NE(menu_context, nullptr);

  // Both bind Space to an action that consumes it. The menu is listed after
  // the gameplay context, so only its higher priority evaluates it first, and
  // only an active context triggers.
  platform::InputSlots::Initialize();
  const auto walk
    = std::make_shared<input::Action>("Walk", input::ActionValueType::kBool);
  const auto select
    = std::make_shared<input::Action>("Select", input::ActionValueType::kBool);
  walk->SetConsumesInput(true);
  select->SetConsumesInput(true);
  Input().AddAction(walk);
  Input().AddAction(select);
  Bind(*gameplay_context, walk, platform::InputSlots::Space);
  Bind(*menu_context, select, platform::InputSlots::Space);

  PressSpace();

  EXPECT_TRUE(select->IsTriggered());
  EXPECT_FALSE(walk->IsTriggered());
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

//! A record the scene cannot apply is an issue on its node and the rest of
//! the node loads: here a material override cooked against an older slot
//! layout.
NOLINT_TEST_F(SceneInstantiationTest, StaleMaterialOverrideIsAnIssue)
{
  const auto slot = data::MaterialSlotId::FromStableIdentity("test/slot");
  const auto geometry = MakeGeometry(Key("/Test/Mesh.ogeo"), slot);
  const auto material_key = Key("/Test/Override.omat");
  Loader().AddGeometry(geometry);
  static_cast<void>(Loader().AddMaterial(material_key));

  auto stale_revision = geometry->MaterialSlots().layout_revision;
  stale_revision.front() ^= 1U;

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Mesh");
  auto renderable = world::RenderableRecord {};
  renderable.geometry_key = geometry->GetAssetKey();
  builder.AddComponents(ComponentType::kRenderable, std::vector { renderable });
  builder.AddComponents(ComponentType::kMaterialOverride,
    std::vector {
      world::MaterialOverrideRecord {
        .node_index = 0U,
        .slot_id = slot,
        .material_key = material_key,
        .layout_revision = stale_revision,
      },
    });

  const auto instance = Run(builder.Build());

  ASSERT_EQ(instance.nodes.size(), 1U);
  EXPECT_EQ(instance.nodes.at(0).GetRenderable().GetGeometry(), geometry);
  const auto stale = std::ranges::find_if(
    instance.issues, [](const SceneInstantiationIssue& issue) -> bool {
      return issue.message.contains("material override");
    });
  ASSERT_NE(stale, instance.issues.end());
  EXPECT_EQ(stale->node_index, 0U);
}

} // namespace oxygen::instantiation::test
