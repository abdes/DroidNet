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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/test/Fakes/StubAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/CodeInput.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
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
#include <Oxygen/Scene/SceneInstanceComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
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
  constexpr float kLodDistance = 12.0F;
  constexpr float kLodHysteresis = 0.2F;

  auto Key(const std::string_view path) -> AssetKey
  {
    return AssetKey::FromVirtualPath(path);
  }

  auto Camera(const uint32_t node) -> world::PerspectiveCameraRecord
  {
    auto record = world::MakeDefaultRecord<world::PerspectiveCameraRecord>();
    record.node_index = node;
    return record;
  }

  //! A one-triangle geometry whose only submesh uses `surface`.
  auto MakeGeometry(const AssetKey& key, const data::SurfaceId surface)
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
      .WithSurfaceId(surface)
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
    auto context = std::make_shared<data::InputMappingContextAsset>(
      key, data::pak::input::InputMappingContextAssetDesc {});
    context->SetAssetName(std::string { name });
    return context;
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

    //! Registers a script declaring the node parameter `target`.
    auto AddScript(const AssetKey& key) -> void
    {
      scripts_.insert_or_assign(key,
        std::make_shared<data::ScriptAsset>(key,
          data::pak::scripting::ScriptAssetDesc {},
          data::ScriptParameters {
            { std::string { "target" }, data::ScriptNodeParam {} },
          }));
    }

    auto GetScriptAsset(const AssetKey& key, const data::Asset& /*context*/)
      -> std::shared_ptr<data::ScriptAsset> override
    {
      const auto it = scripts_.find(key);
      return it != scripts_.end() ? it->second : nullptr;
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
    std::unordered_map<AssetKey, std::shared_ptr<data::ScriptAsset>> scripts_;
  };

  //! Resolves every script to embedded bytecode.
  class BytecodeResolver final : public scripting::IScriptSourceResolver {
  public:
    [[nodiscard]] auto Resolve(const ResolveRequest& /*request*/) const
      -> ResolveResult override
    {
      return {
        .ok = true,
        .blob = scripting::ScriptBytecodeBlob::FromOwned({ 1, 2, 3 },
          data::pak::scripting::ScriptLanguage::kLuau, {}, 0,
          scripting::ScriptBlobOrigin::kEmbeddedResource,
          scripting::ScriptBlobCanonicalName { "bytecode" }),
        .error_message = {},
      };
    }
  };

  class SceneInstantiationTest : public ::testing::Test {
  protected:
    auto Run(std::shared_ptr<const data::SceneAsset> asset)
      -> InstantiationResult
    {
      co::testing::TestEventLoop loop;
      return co::Run(loop,
        Instantiate(
          SceneInstantiationRequest {
            .asset = std::move(asset),
            .target = observer_ptr { scene_.get() },
            .scripts = Scripts(),
            .input = observer_ptr { &input_ },
          },
          loader_));
    }

    //! Instances `source` under the live node `root`.
    auto Place(const scene::SceneNode& root, const AssetKey& source,
      std::vector<InstanceChange> changes = {}) -> InstantiationResult
    {
      co::testing::TestEventLoop loop;
      return co::Run(loop,
        InstantiateUnder(
          InstanceRequest {
            .target = observer_ptr { scene_.get() },
            .root = root,
            .source = source,
            .changes = std::move(changes),
            .scripts = Scripts(),
          },
          loader_));
    }

    //! Attaches the scripts of the scenes instantiated from now on.
    auto UseScripts() -> void { scripted_ = true; }

    [[nodiscard]] auto Scripts() const
      -> observer_ptr<const scripting::SceneScriptAttachment>
    {
      return scripted_
        ? observer_ptr { &scripts_ }
        : observer_ptr<const scripting::SceneScriptAttachment> {};
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
    BytecodeResolver resolver_;
    scripting::SceneScriptAttachment scripts_ { loader_, resolver_, nullptr };
    bool scripted_ = false;
    co::BroadcastChannel<platform::InputEvent> input_channel_;
    engine::InputSystem input_ { input_channel_.ForRead() };
  };

  auto FindIssue(const InstantiationResult& instance,
    const std::string_view code) -> const SceneInstantiationIssue*
  {
    const auto found = std::ranges::find(
      instance.issues, code, &SceneInstantiationIssue::code);
    return found != instance.issues.end() ? &*found : nullptr;
  }

  auto HasIssue(
    const InstantiationResult& instance, const std::string_view code) -> bool
  {
    return FindIssue(instance, code) != nullptr;
  }

  //! Adds the node path tables of `paths` to `builder`.
  auto AddPaths(SceneAssetBuilder& builder,
    const std::vector<std::vector<data::NodeId>>& paths) -> void
  {
    auto records = std::vector<world::NodePathRecord> {};
    auto elements = std::vector<world::NodePathElement> {};
    for (const auto& path : paths) {
      records.push_back({
        .first = static_cast<uint32_t>(elements.size()),
        .count = static_cast<uint32_t>(path.size()),
      });
      for (const auto& id : path) {
        elements.push_back({ .node_id = id });
      }
    }
    builder.AddComponents(ComponentType::kNodePath, records);
    builder.AddComponents(ComponentType::kNodePathElement, elements);
  }

  //! A change of a float property below the instance root `root`.
  auto FloatChange(const uint32_t root, const uint32_t path,
    const data::schema::OverrideId id, const float value)
    -> world::OverrideRecord
  {
    auto record = world::OverrideRecord {
      .target = { .node_index = root, .node_path = path },
      .override_id = id,
    };
    record.value.as_float = value;
    return record;
  }

  auto LocalX(scene::SceneNode& node) -> float
  {
    const auto position = node.GetTransform().GetLocalPosition();
    return position.has_value() ? position->x : 0.0F;
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
  EXPECT_TRUE(HasIssue(instance, issue_codes::kStartingCameraMissing));
}

//! An instanced scene's nodes are created under the instance node; a missing
//! instanced scene is an issue and the rest of the scene loads.
NOLINT_TEST_F(SceneInstantiationTest, InstancedScenesLoadUnderTheirNodes)
{
  const auto model_key = Key("/test/Model.oscene");
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
        .scene_key = Key("/test/Missing.oscene"),
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
  EXPECT_TRUE(HasIssue(
    instance, data::to_string(data::CompositionIssueCode::kSceneMissing)));
}

namespace {
  //! A surface material override of `surface` on `node`.
  auto MaterialOverride(const uint32_t node, const data::SurfaceId surface,
    const data::AssetKey& material, const base::Sha256Digest& layout_revision)
    -> world::OverrideRecord
  {
    auto record = world::OverrideRecord {
      .target = { .node_index = node },
      .override_id = data::schema::OverrideId::kGeometrySurfaceMaterial,
      .part_id = surface,
      .layout_revision = layout_revision,
    };
    record.value.as_asset = material;
    return record;
  }
} // namespace

//! Surface Visibility and Cast Shadows overrides and the LOD policy override
//! reach the renderable; the LOD policy value is read from its record table.
NOLINT_TEST_F(SceneInstantiationTest, AppliesSurfaceAndLodPolicyOverrides)
{
  const auto surface = data::SurfaceId::FromStableIdentity("test/slot");
  const auto geometry = MakeGeometry(Key("/test/Mesh.ogeo"), surface);
  Loader().AddGeometry(geometry);
  const auto revision = geometry->Surfaces().layout_revision;

  auto builder = SceneAssetBuilder {};
  const auto hidden = builder.AddNode("Hidden");
  const auto unlit = builder.AddNode("Unlit");
  auto renderables = std::vector<world::RenderableRecord> {};
  for (const auto node : { hidden, unlit }) {
    renderables.push_back({
      .node_index = node,
      .geometry_key = geometry->GetAssetKey(),
    });
  }
  builder.AddComponents(ComponentType::kRenderable, std::move(renderables));
  auto visibility = world::OverrideRecord {
    .target = { .node_index = hidden },
    .override_id = data::schema::OverrideId::kGeometrySurfaceVisibility,
    .part_id = surface,
    .layout_revision = revision,
  };
  visibility.value.as_bool = 0U;
  auto shadows = world::OverrideRecord {
    .target = { .node_index = unlit },
    .override_id = data::schema::OverrideId::kGeometrySurfaceCastsShadows,
    .part_id = surface,
    .layout_revision = revision,
  };
  shadows.value.as_bool = 0U;
  auto lod_policy = world::OverrideRecord {
    .target = { .node_index = unlit },
    .override_id = data::schema::OverrideId::kGeometryLodPolicy,
  };
  lod_policy.value.as_record_index = world::OverrideRecordIndex { 0U };
  builder.AddComponents(
    ComponentType::kOverride, std::vector { visibility, shadows, lod_policy });
  auto distance = data::pak::geometry::LodPolicyRecord {
    .type = data::pak::geometry::LodPolicyType::kDistance,
    .boundary_count = 1U,
  };
  distance.thresholds[0] = kLodDistance;
  distance.hysteresis_ratio = kLodHysteresis;
  builder.AddComponents(
    ComponentType::kOverrideLodPolicy, std::vector { distance });

  const auto instance = Run(builder.Build());

  // The scene has no camera, which is an issue of its own.
  EXPECT_FALSE(HasIssue(instance, issue_codes::kOverrideNotApplied));
  const auto first = data::LodIndex { 0U };
  const auto submesh = data::SubmeshIndex { 0U };
  const auto hidden_renderable = instance.nodes.at(hidden).GetRenderable();
  EXPECT_FALSE(hidden_renderable.IsSubmeshVisible(first, submesh));
  EXPECT_FALSE(hidden_renderable.HasLodPolicyOverride());
  const auto unlit_renderable = instance.nodes.at(unlit).GetRenderable();
  EXPECT_TRUE(unlit_renderable.IsSubmeshVisible(first, submesh));
  EXPECT_FALSE(unlit_renderable.SubmeshCastsShadows(first, submesh));
  EXPECT_TRUE(unlit_renderable.HasLodPolicyOverride());
  const auto policy = unlit_renderable.GetLodPolicy();
  ASSERT_TRUE(policy.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  const auto* resolved = std::get_if<scene::DistancePolicy>(&policy.value());
  ASSERT_NE(resolved, nullptr);
  ASSERT_EQ(resolved->thresholds.size(), 1U);
  EXPECT_FLOAT_EQ(resolved->thresholds.front(), kLodDistance);
}

//! Renderables with their material overrides, both camera kinds, every light
//! kind and local fog volumes are attached with their authored values.
NOLINT_TEST_F(SceneInstantiationTest, AttachesEveryNodeRecordFamily)
{
  const auto surface = data::SurfaceId::FromStableIdentity("test/slot");
  const auto geometry = MakeGeometry(Key("/test/Mesh.ogeo"), surface);
  const auto material_key = Key("/test/Override.omat");
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
  builder.AddComponents(ComponentType::kOverride,
    std::vector {
      MaterialOverride(
        mesh, surface, material_key, geometry->Surfaces().layout_revision),
    });
  auto perspective = Camera(eye);
  perspective.fov_y = kFieldOfView;
  builder.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { perspective });
  auto orthographic
    = world::MakeDefaultRecord<world::OrthographicCameraRecord>();
  orthographic.node_index = map;
  orthographic.left = -kOrthoHalfWidth;
  orthographic.right = kOrthoHalfWidth;
  builder.AddComponents(
    ComponentType::kOrthographicCamera, std::vector { orthographic });
  auto directional = world::MakeDefaultRecord<world::DirectionalLightRecord>();
  directional.node_index = sun;
  directional.intensity_lux = kSunLux;
  builder.AddComponents(
    ComponentType::kDirectionalLight, std::vector { directional });
  auto point = world::MakeDefaultRecord<world::PointLightRecord>();
  point.node_index = bulb;
  point.luminous_flux_lm = kBulbFlux;
  builder.AddComponents(ComponentType::kPointLight, std::vector { point });
  auto spot_light = world::MakeDefaultRecord<world::SpotLightRecord>();
  spot_light.node_index = spot;
  spot_light.outer_cone_angle_radians = kSpotOuterCone;
  builder.AddComponents(ComponentType::kSpotLight, std::vector { spot_light });
  auto fog = world::MakeDefaultRecord<world::LocalFogVolumeRecord>();
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
  const auto model_key = Key("/test/Model.oscene");
  const auto model_context = Key("/test/Model.oimap");
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
  const auto gameplay = Key("/test/Gameplay.oimap");
  const auto menu = Key("/test/Menu.oimap");
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
        .context_key = Key("/test/Missing.oimap"),
        .priority = 0,
      },
      world::StartingInputContextRecord {
        .context_key = menu,
        .priority = kMenuPriority,
      },
    });

  const auto instance = Run(builder.Build());

  EXPECT_TRUE(HasIssue(instance, issue_codes::kInputContextInvalid));
  const auto gameplay_context = Input().GetMappingContextByKey(gameplay);
  const auto menu_context = Input().GetMappingContextByKey(menu);
  ASSERT_NE(gameplay_context, nullptr);
  ASSERT_NE(menu_context, nullptr);

  // Both bind Space to an action that consumes it. The menu is listed after
  // the gameplay context, so only its higher priority evaluates it first, and
  // only an active context triggers.
  platform::InputSlots::Initialize();
  constexpr auto kInputOwner = std::string_view { "Test/SceneInstantiation" };
  const auto walk
    = input::MakeCodeAction(kInputOwner, "Walk", input::ActionValueType::kBool);
  const auto select = input::MakeCodeAction(
    kInputOwner, "Select", input::ActionValueType::kBool);
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
  renderable.geometry_key = Key("/test/Mesh.ogeo");
  builder.AddComponents(ComponentType::kRenderable, std::vector { renderable });

  const auto instance = Run(builder.Build());

  ASSERT_EQ(instance.nodes.size(), 1U);
  ASSERT_FALSE(instance.issues.empty());
  EXPECT_EQ(instance.issues.front().node_index, 0U);
  EXPECT_TRUE(HasIssue(instance, issue_codes::kGeometryMissing));
}

//! A record the scene cannot apply is an issue on its node and the rest of
//! the node loads: here a material override cooked against an older surface
//! layout.
NOLINT_TEST_F(SceneInstantiationTest, StaleMaterialOverrideIsAnIssue)
{
  const auto surface = data::SurfaceId::FromStableIdentity("test/slot");
  const auto geometry = MakeGeometry(Key("/test/Mesh.ogeo"), surface);
  const auto material_key = Key("/test/Override.omat");
  Loader().AddGeometry(geometry);
  static_cast<void>(Loader().AddMaterial(material_key));

  auto stale_revision = geometry->Surfaces().layout_revision;
  stale_revision.front() ^= 1U;

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Mesh");
  auto renderable = world::RenderableRecord {};
  renderable.geometry_key = geometry->GetAssetKey();
  builder.AddComponents(ComponentType::kRenderable, std::vector { renderable });
  builder.AddComponents(ComponentType::kOverride,
    std::vector {
      MaterialOverride(0U, surface, material_key, stale_revision),
    });

  const auto instance = Run(builder.Build());

  ASSERT_EQ(instance.nodes.size(), 1U);
  EXPECT_EQ(instance.nodes.at(0).GetRenderable().GetGeometry(), geometry);
  const auto* stale = FindIssue(instance, issue_codes::kOverrideNotApplied);
  ASSERT_NE(stale, nullptr);
  EXPECT_EQ(stale->node_index, 0U);
}

namespace {
  constexpr float kModelX = 1.0F;
  constexpr float kChangedX = 2.0F;
  constexpr float kOuterX = 3.0F;
  constexpr uint32_t kWheel = 1U;

  //! A model `Body` with a child `Wheel` at x = 1.
  auto WheelModel() -> SceneAssetBuilder
  {
    auto model = SceneAssetBuilder {};
    model.AddNode("Body");
    model.AddNode("Wheel", 0U);
    model.Node(kWheel).translation[0] = kModelX;
    model.SetFlags(world::SceneAssetFlags::kModelScene);
    return model;
  }
} // namespace

//! A scene that places a model twice builds both instances; a change applies
//! to its own instance only, and Find returns each instanced node.
NOLINT_TEST_F(SceneInstantiationTest, PlacesAModelTwice)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Root");
  const auto left = builder.AddNode("Left", 0U);
  const auto right = builder.AddNode("Right", 0U);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord { .node_index = left, .scene_key = model_key },
      world::SceneInstanceRecord {
        .node_index = right,
        .scene_key = model_key,
      },
    });
  AddPaths(builder, { { wheel } });
  builder.AddComponents(ComponentType::kOverride,
    std::vector {
      FloatChange(
        right, 0U, data::schema::OverrideId::kTransformPositionX, kChangedX),
    });

  const auto instance = Run(builder.Build());

  EXPECT_FALSE(HasIssue(instance, issue_codes::kChangeUnresolved));
  auto left_root = instance.nodes.at(left);
  auto right_root = instance.nodes.at(right);
  ASSERT_TRUE(left_root.GetInstance().IsInstanceRoot());
  ASSERT_TRUE(right_root.GetInstance().IsInstanceRoot());
  EXPECT_TRUE(left_root.GetInstance().Changes().empty());
  EXPECT_EQ(right_root.GetInstance().Changes().size(), 1U);
  EXPECT_EQ(right_root.GetInstance().Source()->GetAssetKey(), model_key);
  const auto path = std::vector { wheel };
  auto left_wheel = left_root.GetInstance().Find(path);
  auto right_wheel = right_root.GetInstance().Find(path);
  ASSERT_TRUE(left_wheel.has_value());
  ASSERT_TRUE(right_wheel.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(left_wheel->GetName(), "Wheel");
  EXPECT_NE(left_wheel->GetHandle(), right_wheel->GetHandle());
  EXPECT_FLOAT_EQ(LocalX(*left_wheel), kModelX);
  EXPECT_FLOAT_EQ(LocalX(*right_wheel), kChangedX);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

//! Nesting two levels deep builds the composed tree: the outer scene's
//! change of a node two levels down wins over the nested scene's, and Find
//! follows the path one instance root per level.
NOLINT_TEST_F(SceneInstantiationTest, BuildsTwoNestingLevels)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));

  const auto car_key = Key("/test/Car.oscene");
  auto car = SceneAssetBuilder {};
  car.AddNode("CarRoot");
  const auto holder = car.AddNode("Holder", 0U);
  const auto holder_id = car.Node(holder).node_id;
  car.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = holder,
        .scene_key = model_key,
      },
    });
  AddPaths(car, { { wheel } });
  car.AddComponents(ComponentType::kOverride,
    std::vector {
      FloatChange(
        holder, 0U, data::schema::OverrideId::kTransformPositionX, kChangedX),
    });
  Loader().AddScene(car_key, car.Build(car_key));

  auto street = SceneAssetBuilder {};
  street.AddNode("Street");
  const auto parked = street.AddNode("Parked", 0U);
  street.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord { .node_index = parked, .scene_key = car_key },
    });
  AddPaths(street, { { holder_id, wheel } });
  street.AddComponents(ComponentType::kOverride,
    std::vector {
      FloatChange(
        parked, 0U, data::schema::OverrideId::kTransformPositionX, kOuterX),
    });

  const auto instance = Run(street.Build());

  auto root = instance.nodes.at(parked);
  const auto path = std::vector { holder_id, wheel };
  auto found = root.GetInstance().Find(path);
  ASSERT_TRUE(found.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(LocalX(*found), kOuterX);
  auto nested = root.GetInstance().Find(std::span(path).first(1U));
  ASSERT_TRUE(nested.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(nested->GetInstance().Source()->GetAssetKey(), model_key);
}

//! A change that does not resolve is an issue naming its path; the rest of
//! the scene builds.
NOLINT_TEST_F(SceneInstantiationTest, UnresolvedChangeIsAnIssue)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  const auto stranger = data::NodeId::FromStableIdentity("/test/Stranger");

  auto builder = SceneAssetBuilder {};
  builder.AddNode("Root");
  const auto holder = builder.AddNode("Holder", 0U);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = holder,
        .scene_key = model_key,
      },
    });
  AddPaths(builder, { { stranger } });
  builder.AddComponents(ComponentType::kOverride,
    std::vector {
      FloatChange(
        holder, 0U, data::schema::OverrideId::kTransformPositionX, kChangedX),
    });

  const auto instance = Run(builder.Build());

  const auto* issue = FindIssue(instance, issue_codes::kChangeUnresolved);
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->node_index, holder);
  EXPECT_EQ(issue->node_path, std::vector { stranger });
  auto root = instance.nodes.at(holder);
  auto found = root.GetInstance().Find(std::vector { wheel });
  ASSERT_TRUE(found.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(LocalX(*found), kModelX);
}

namespace {
  //! A camera rig whose node 1, `RigEye`, has a camera when `with_camera`.
  auto CameraRig(const bool with_camera) -> SceneAssetBuilder
  {
    auto rig = SceneAssetBuilder {};
    rig.AddNode("Rig");
    rig.AddNode("RigEye", 0U);
    if (with_camera) {
      rig.AddComponents(
        ComponentType::kPerspectiveCamera, std::vector { Camera(1U) });
    }
    return rig;
  }

  //! A scene with its own camera first and a placed rig whose eye is the
  //! starting camera.
  auto StartsThroughTheRig(const AssetKey& rig_key, const data::NodeId& eye)
    -> SceneAssetBuilder
  {
    auto builder = SceneAssetBuilder {};
    builder.AddNode("Eye");
    const auto holder = builder.AddNode("RigHolder");
    builder.AddComponents(
      ComponentType::kPerspectiveCamera, std::vector { Camera(0U) });
    builder.AddComponents(ComponentType::kSceneInstance,
      std::vector {
        world::SceneInstanceRecord {
          .node_index = holder,
          .scene_key = rig_key,
        },
      });
    AddPaths(builder, { { eye } });
    builder.SetStartingCamera(
      world::NodeRef { .node_index = holder, .node_path = 0U });
    return builder;
  }
} // namespace

//! A scene whose starting camera is a camera of a placed rig starts through
//! it.
NOLINT_TEST_F(SceneInstantiationTest, StartsThroughAnInstancedCamera)
{
  const auto rig_key = Key("/test/CameraRig.oscene");
  auto rig = CameraRig(true);
  Loader().AddScene(rig_key, rig.Build(rig_key));

  const auto instance
    = Run(StartsThroughTheRig(rig_key, rig.Node(1U).node_id).Build());

  EXPECT_FALSE(HasIssue(instance, issue_codes::kStartingCameraUnresolved));
  const auto active = Scene().ActiveCamera();
  ASSERT_TRUE(active.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(active->GetName(), "RigEye");
}

//! With the camera gone from the rig, the scene starts through its first
//! camera, with an issue.
NOLINT_TEST_F(SceneInstantiationTest, MissingInstancedCameraFallsBack)
{
  const auto rig_key = Key("/test/CameraRig.oscene");
  auto rig = CameraRig(false);
  Loader().AddScene(rig_key, rig.Build(rig_key));

  const auto instance
    = Run(StartsThroughTheRig(rig_key, rig.Node(1U).node_id).Build());

  EXPECT_TRUE(HasIssue(instance, issue_codes::kStartingCameraUnresolved));
  const auto active = Scene().ActiveCamera();
  ASSERT_TRUE(active.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(active->GetName(), "Eye");
}

namespace {
  constexpr float kInnerX = 5.0F;

  //! A change of the local x of the node `path` names, in schema units.
  auto MoveX(std::vector<data::NodeId> path, const float x) -> InstanceChange
  {
    return InstanceChange {
      .path = std::move(path),
      .change = PropertyValue {
        .override_id = data::schema::OverrideId::kTransformPositionX,
        .value = x,
      },
    };
  }

  //! The address of the local x of the node `path` names.
  auto XAddress(std::vector<data::NodeId> path) -> data::ChangeAddress
  {
    return data::ChangeAddress {
      .path = std::move(path),
      .target = data::PropertyAddress {
        .override_id = data::schema::OverrideId::kTransformPositionX,
      },
    };
  }

  //! A street that parks a car whose `Holder` instances the wheel model; the
  //! car moves the wheel to `kChangedX`. Registers the car and the model.
  struct Street {
    AssetKey model_key;
    data::NodeId holder_id;
    data::NodeId wheel;
    uint32_t parked = 0;
    std::shared_ptr<data::SceneAsset> asset;
  };

  auto MakeStreet(TestLoader& loader) -> Street
  {
    auto street = Street { .model_key = Key("/test/Wheel.oscene") };
    auto model = WheelModel();
    street.wheel = model.Node(kWheel).node_id;
    loader.AddScene(street.model_key, model.Build(street.model_key));

    const auto car_key = Key("/test/Car.oscene");
    auto car = SceneAssetBuilder {};
    car.AddNode("CarRoot");
    const auto holder = car.AddNode("Holder", 0U);
    street.holder_id = car.Node(holder).node_id;
    car.AddComponents(ComponentType::kSceneInstance,
      std::vector {
        world::SceneInstanceRecord {
          .node_index = holder,
          .scene_key = street.model_key,
        },
      });
    AddPaths(car, { { street.wheel } });
    car.AddComponents(ComponentType::kOverride,
      std::vector {
        FloatChange(
          holder, 0U, data::schema::OverrideId::kTransformPositionX, kChangedX),
      });
    loader.AddScene(car_key, car.Build(car_key));

    auto builder = SceneAssetBuilder {};
    builder.AddNode("Street");
    street.parked = builder.AddNode("Parked", 0U);
    builder.AddComponents(ComponentType::kSceneInstance,
      std::vector {
        world::SceneInstanceRecord {
          .node_index = street.parked,
          .scene_key = car_key,
        },
      });
    street.asset = builder.Build();
    return street;
  }
} // namespace

//! A scene instanced under a live node builds under it with the host's
//! changes; the node becomes an instance root.
NOLINT_TEST_F(SceneInstantiationTest, InstancesUnderALiveNode)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto host = Scene().CreateNode("Host");

  const auto placed = Place(host, model_key, { MoveX({ wheel }, kChangedX) });

  EXPECT_TRUE(placed.issues.empty());
  ASSERT_EQ(placed.nodes.size(), 2U);
  ASSERT_TRUE(host.GetInstance().IsInstanceRoot());
  EXPECT_EQ(host.GetInstance().Source()->GetAssetKey(), model_key);
  EXPECT_EQ(host.GetInstance().Changes().size(), 1U);
  auto body = placed.nodes.at(0);
  const auto parent = body.GetParent();
  ASSERT_TRUE(parent.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(parent->GetHandle(), host.GetHandle());
  auto found = host.GetInstance().Find(std::vector { wheel });
  ASSERT_TRUE(found.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(LocalX(*found), kChangedX);
}

//! An instance root cannot be instanced under, and a value that is not a
//! value of its property is an issue with no effect.
NOLINT_TEST_F(SceneInstantiationTest, InstancingUnderRefusesBadInput)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto host = Scene().CreateNode("Host");
  auto bad = MoveX({ wheel }, kChangedX);
  std::get<PropertyValue>(bad.change).value = true;

  const auto placed = Place(host, model_key, { bad });
  const auto again = Place(host, model_key);

  EXPECT_TRUE(HasIssue(placed, "scene.instance.change_invalid"));
  EXPECT_TRUE(host.GetInstance().Changes().empty());
  EXPECT_TRUE(HasIssue(again, issue_codes::kInstanceRootInvalid));
  EXPECT_TRUE(again.nodes.empty());
}

//! Instancing a scene under a node one of its own instances brings in is a
//! cycle and builds nothing.
NOLINT_TEST_F(SceneInstantiationTest, InstancingUnderItselfIsACycle)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  Loader().AddScene(model_key, model.Build(model_key));
  auto host = Scene().CreateNode("Host");
  const auto placed = Place(host, model_key);
  auto body = placed.nodes.at(0);

  const auto nested = Place(body, model_key);

  EXPECT_TRUE(HasIssue(nested, "scene.instance.cycle"));
  EXPECT_TRUE(nested.nodes.empty());
  EXPECT_FALSE(body.GetInstance().IsInstanceRoot());
}

//! Setting then clearing a change restores the source value.
NOLINT_TEST_F(SceneInstantiationTest, SetThenClearRestoresTheSourceValue)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto host = Scene().CreateNode("Host");
  (void)Place(host, model_key);
  auto found = host.GetInstance().Find(std::vector { wheel });
  ASSERT_TRUE(found.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above

  const auto set = SetChange(host, MoveX({ wheel }, kChangedX), Loader());
  const auto changed_x = LocalX(*found);
  const auto cleared = ClearChange(host, XAddress({ wheel }), Loader());

  EXPECT_TRUE(set.issues.empty());
  EXPECT_FLOAT_EQ(changed_x, kChangedX);
  EXPECT_TRUE(cleared.issues.empty());
  EXPECT_FLOAT_EQ(LocalX(*found), kModelX);
  EXPECT_TRUE(host.GetInstance().Changes().empty());
  // NOLINTEND(bugprone-unchecked-optional-access)
}

//! Two levels down, clearing the outer change restores the nested scene's
//! change; a change of the nested root does not beat the outer one.
NOLINT_TEST_F(SceneInstantiationTest, SetThenClearAtTheNestedLevel)
{
  const auto street = MakeStreet(Loader());
  const auto instance = Run(street.asset);
  auto root = instance.nodes.at(street.parked);
  const auto path = std::vector { street.holder_id, street.wheel };
  auto found = root.GetInstance().Find(path);
  auto holder = root.GetInstance().Find(std::span(path).first(1U));
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(holder.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above

  const auto outer = SetChange(root, MoveX(path, kOuterX), Loader());
  const auto outer_x = LocalX(*found);
  const auto inner
    = SetChange(*holder, MoveX({ street.wheel }, kInnerX), Loader());
  const auto inner_x = LocalX(*found);
  const auto cleared = ClearChange(root, XAddress(path), Loader());
  const auto cleared_x = LocalX(*found);
  const auto inner_cleared
    = ClearChange(*holder, XAddress({ street.wheel }), Loader());

  EXPECT_TRUE(outer.issues.empty());
  EXPECT_FLOAT_EQ(outer_x, kOuterX);
  EXPECT_TRUE(inner.issues.empty());
  EXPECT_FLOAT_EQ(inner_x, kOuterX);
  EXPECT_TRUE(cleared.issues.empty());
  EXPECT_FLOAT_EQ(cleared_x, kInnerX);
  EXPECT_TRUE(inner_cleared.issues.empty());
  EXPECT_FLOAT_EQ(LocalX(*found), kModelX);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

//! A change whose address does not resolve returns an issue and changes
//! nothing; so does clearing a change the instance does not hold.
NOLINT_TEST_F(SceneInstantiationTest, UnresolvedLiveChangeChangesNothing)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto host = Scene().CreateNode("Host");
  (void)Place(host, model_key, { MoveX({ wheel }, kChangedX) });
  const auto stranger = data::NodeId::FromStableIdentity("/test/Stranger");
  auto plain = Scene().CreateNode("Plain");

  const auto set = SetChange(host, MoveX({ stranger }, kOuterX), Loader());
  const auto through
    = SetChange(host, MoveX({ wheel, wheel }, kOuterX), Loader());
  const auto clear = ClearChange(host, XAddress({ stranger }), Loader());
  const auto not_root = SetChange(plain, MoveX({ wheel }, kOuterX), Loader());

  EXPECT_FALSE(set.issues.empty());
  EXPECT_EQ(set.issues.front().code, issue_codes::kChangeUnresolved);
  EXPECT_EQ(set.issues.front().node_path, std::vector { stranger });
  EXPECT_FALSE(through.issues.empty());
  EXPECT_FALSE(clear.issues.empty());
  EXPECT_FALSE(not_root.issues.empty());
  EXPECT_EQ(not_root.issues.front().code, issue_codes::kNotInstanceRoot);
  ASSERT_EQ(host.GetInstance().Changes().size(), 1U);
  auto found = host.GetInstance().Find(std::vector { wheel });
  ASSERT_TRUE(found.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FLOAT_EQ(LocalX(*found), kChangedX);
}

//! A surface change set live applies to the instanced renderable; clearing
//! it removes the override.
NOLINT_TEST_F(SceneInstantiationTest, SurfaceChangeSetsAndClearsLive)
{
  const auto surface = data::SurfaceId::FromStableIdentity("test/slot");
  const auto geometry = MakeGeometry(Key("/test/Mesh.ogeo"), surface);
  Loader().AddGeometry(geometry);
  const auto model_key = Key("/test/Prop.oscene");
  auto model = SceneAssetBuilder {};
  model.AddNode("Prop");
  model.AddComponents(ComponentType::kRenderable,
    std::vector {
      world::RenderableRecord {
        .node_index = 0U,
        .geometry_key = geometry->GetAssetKey(),
      },
    });
  const auto prop = model.Node(0U).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto builder = SceneAssetBuilder {};
  const auto holder = builder.AddNode("Holder");
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = holder,
        .scene_key = model_key,
      },
    });
  const auto instance = Run(builder.Build());
  auto root = instance.nodes.at(holder);
  auto found = root.GetInstance().Find(std::vector { prop });
  ASSERT_TRUE(found.has_value());
  const auto first = data::LodIndex { 0U };
  const auto submesh = data::SubmeshIndex { 0U };
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above

  const auto set = SetChange(root,
    InstanceChange {
      .path = { prop },
      .change = PropertyValue {
        .override_id = data::schema::OverrideId::kGeometrySurfaceVisibility,
        .part_id = surface,
        .value = false,
      },
    },
    Loader());
  const auto hidden = !found->GetRenderable().IsSubmeshVisible(first, submesh);
  const auto cleared = ClearChange(root,
    data::ChangeAddress {
      .path = { prop },
      .target = data::PropertyAddress {
        .override_id = data::schema::OverrideId::kGeometrySurfaceVisibility,
        .part_id = surface,
      },
    },
    Loader());

  EXPECT_TRUE(set.issues.empty());
  EXPECT_TRUE(hidden);
  EXPECT_TRUE(cleared.issues.empty());
  EXPECT_TRUE(found->GetRenderable().IsSubmeshVisible(first, submesh));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

//! A host's LOD policy applies when the instance is placed; a live change
//! replaces it in place, and clearing it restores the geometry's policy.
NOLINT_TEST_F(SceneInstantiationTest, HostLodPolicySetsAndClearsLive)
{
  const auto geometry = MakeGeometry(
    Key("/test/Mesh.ogeo"), data::SurfaceId::FromStableIdentity("test/slot"));
  Loader().AddGeometry(geometry);
  const auto model_key = Key("/test/Prop.oscene");
  auto model = SceneAssetBuilder {};
  model.AddNode("Prop");
  model.AddComponents(ComponentType::kRenderable,
    std::vector {
      world::RenderableRecord {
        .node_index = 0U,
        .geometry_key = geometry->GetAssetKey(),
      },
    });
  const auto prop = model.Node(0U).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  const auto distance = [](const float threshold) -> LodPolicyValue {
    auto policy = data::pak::geometry::LodPolicyRecord {
      .type = data::pak::geometry::LodPolicyType::kDistance,
      .boundary_count = 1U,
    };
    policy.thresholds[0] = threshold;
    policy.hysteresis_ratio = kLodHysteresis;
    return LodPolicyValue { .policy = policy };
  };
  const auto threshold = [](scene::SceneNode& node) -> float {
    const auto policy = node.GetRenderable().GetLodPolicy();
    const auto* resolved = policy.has_value()
      ? std::get_if<scene::DistancePolicy>(&policy.value())
      : nullptr;
    return resolved != nullptr && !resolved->thresholds.empty()
      ? resolved->thresholds.front()
      : 0.0F;
  };
  auto host = Scene().CreateNode("Host");

  const auto placed = Place(host, model_key,
    { InstanceChange { .path = { prop }, .change = distance(kLodDistance) } });
  auto found = host.GetInstance().Find(std::vector { prop });
  ASSERT_TRUE(found.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
  const auto placed_threshold = threshold(*found);
  const auto set = SetChange(host,
    InstanceChange {
      .path = { prop },
      .change = distance(2.0F * kLodDistance),
    },
    Loader());
  const auto set_threshold = threshold(*found);
  const auto held = host.GetImpl()
                      ->get()
                      .GetComponent<scene::SceneInstanceComponent>()
                      .HostLodPolicies()
                      .size();
  const auto cleared = ClearChange(host,
    data::ChangeAddress {
      .path = { prop },
      .target = data::PropertyAddress {
        .override_id = data::schema::OverrideId::kGeometryLodPolicy,
      },
    },
    Loader());

  EXPECT_TRUE(placed.issues.empty());
  EXPECT_FLOAT_EQ(placed_threshold, kLodDistance);
  EXPECT_TRUE(set.issues.empty());
  EXPECT_FLOAT_EQ(set_threshold, 2.0F * kLodDistance);
  EXPECT_EQ(held, 1U) << "a new value of the same change reuses its policy";
  EXPECT_TRUE(cleared.issues.empty());
  EXPECT_FALSE(found->GetRenderable().HasLodPolicyOverride());
  // NOLINTEND(bugprone-unchecked-optional-access)
}

//! Detach destroys what the instance brought in and keeps the root and its
//! owned children.
NOLINT_TEST_F(SceneInstantiationTest, DetachKeepsTheRootAndOwnedChildren)
{
  const auto model_key = Key("/test/Wheel.oscene");
  auto model = WheelModel();
  const auto wheel = model.Node(kWheel).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto builder = SceneAssetBuilder {};
  const auto holder = builder.AddNode("Holder");
  const auto owned = builder.AddNode("Owned", holder);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = holder,
        .scene_key = model_key,
      },
    });
  const auto instance = Run(builder.Build());
  auto root = instance.nodes.at(holder);
  auto owned_node = instance.nodes.at(owned);
  auto found = root.GetInstance().Find(std::vector { wheel });
  ASSERT_TRUE(found.has_value());

  const auto detached = Detach(Scene(), root);

  EXPECT_TRUE(detached);
  EXPECT_TRUE(root.IsAlive());
  EXPECT_FALSE(root.GetInstance().IsInstanceRoot());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_FALSE(found->IsAlive());
  ASSERT_TRUE(owned_node.IsAlive());
  const auto parent = owned_node.GetParent();
  ASSERT_TRUE(parent.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - asserted above
  EXPECT_EQ(parent->GetHandle(), root.GetHandle());
  EXPECT_FALSE(Detach(Scene(), root));
}

namespace {
  //! A model `Door` whose only node has a slot of `script`.
  auto DoorModel(const AssetKey& script) -> SceneAssetBuilder
  {
    auto model = SceneAssetBuilder {};
    model.AddNode("Door");
    auto slot = data::pak::scripting::ScriptSlotRecord {
      .slot_id = data::ScriptSlotId::FromStableIdentity("/test/slot/door"),
      .script_asset_key = script,
    };
    model.AddScripting(0U, { slot });
    model.SetFlags(world::SceneAssetFlags::kModelScene);
    return model;
  }

  auto DoorSlot() -> data::ScriptSlotId
  {
    return data::ScriptSlotId::FromStableIdentity("/test/slot/door");
  }

  //! A host's change of the door's `target` to `node`.
  auto Target(std::vector<data::NodeId> path, const scene::SceneNode& node)
    -> InstanceChange
  {
    return InstanceChange {
      .path = std::move(path),
      .change = NodeParameterValue {
        .slot_id = DoorSlot(),
        .key = "target",
        .node = node,
      },
    };
  }

  //! The node the first slot's `target` is bound to.
  auto TargetOf(scene::SceneNode& node) -> std::optional<scene::NodeHandle>
  {
    auto scripting = node.GetScripting();
    const auto slots = scripting.Slots();
    if (slots.empty()) {
      return std::nullopt;
    }
    return slots.front().TryGetBoundNode("target");
  }
} // namespace

//! A host's node value binds the live node it names, when instancing and when
//! set live; naming the same node again reuses its host node.
NOLINT_TEST_F(SceneInstantiationTest, HostNodeParameterBindsItsNode)
{
  UseScripts();
  const auto script = Key("/test/Door.oscript");
  Loader().AddScript(script);
  const auto model_key = Key("/test/Door.oscene");
  auto model = DoorModel(script);
  const auto door_id = model.Node(0U).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto garage = Scene().CreateNode("Garage");
  auto shed = Scene().CreateNode("Shed");
  auto host = Scene().CreateNode("Host");

  const auto placed = Place(host, model_key, { Target({ door_id }, garage) });
  auto door = host.GetInstance().Find(std::vector { door_id });
  ASSERT_TRUE(door.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
  const auto placed_target = TargetOf(*door);
  const auto set
    = SetChange(host, Target({ door_id }, shed), Loader(), Scripts());
  const auto set_target = TargetOf(*door);
  const auto again
    = SetChange(host, Target({ door_id }, shed), Loader(), Scripts());

  EXPECT_TRUE(placed.issues.empty());
  EXPECT_EQ(placed_target, garage.GetHandle());
  EXPECT_TRUE(set.issues.empty());
  ASSERT_EQ(set.scripts.size(), 1U);
  EXPECT_TRUE(set.scripts.front().diagnostics.empty());
  EXPECT_EQ(set_target, shed.GetHandle());
  EXPECT_TRUE(again.issues.empty());
  // NOLINTEND(bugprone-unchecked-optional-access)
}

//! A node value given as a scene's node index names no node of a host, so it
//! is invalid and changes nothing.
NOLINT_TEST_F(SceneInstantiationTest, HostNodeIndexIsInvalid)
{
  const auto script = Key("/test/Door.oscript");
  Loader().AddScript(script);
  const auto model_key = Key("/test/Door.oscene");
  auto model = DoorModel(script);
  const auto door_id = model.Node(0U).node_id;
  Loader().AddScene(model_key, model.Build(model_key));
  auto host = Scene().CreateNode("Host");
  (void)Place(host, model_key);

  const auto set = SetChange(host,
    InstanceChange {
      .path = { door_id },
      .change = data::ParameterChange {
        .slot_id = DoorSlot(),
        .key = "target",
        .value = data::ScriptNodeParam { .node_index = 0U },
      },
    },
    Loader());

  ASSERT_FALSE(set.issues.empty());
  EXPECT_EQ(set.issues.front().code, "scene.instance.change_invalid");
  EXPECT_TRUE(host.GetInstance().Changes().empty());
}

//! The outermost scene's node value still binds its own node when a deeper
//! level's change of the parameter is set live; clearing the outer change
//! leaves the deeper one.
NOLINT_TEST_F(SceneInstantiationTest, OuterNodeValueRebindsLive)
{
  UseScripts();
  const auto script = Key("/test/Door.oscript");
  Loader().AddScript(script);
  const auto model_key = Key("/test/Door.oscene");
  auto model = DoorModel(script);
  const auto door_id = model.Node(0U).node_id;
  Loader().AddScene(model_key, model.Build(model_key));

  const auto car_key = Key("/test/Car.oscene");
  auto car = SceneAssetBuilder {};
  car.AddNode("CarRoot");
  const auto holder = car.AddNode("Holder", 0U);
  const auto holder_id = car.Node(holder).node_id;
  car.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = holder,
        .scene_key = model_key,
      },
    });
  Loader().AddScene(car_key, car.Build(car_key));

  auto yard = SceneAssetBuilder {};
  const auto garage = yard.AddNode("Garage");
  const auto parked = yard.AddNode("Parked");
  yard.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord { .node_index = parked, .scene_key = car_key },
    });
  AddPaths(yard, { { holder_id, door_id } });
  auto target = data::pak::scripting::ScriptParamOverrideRecord {
    .target = { .node_index = parked, .node_path = 0U },
    .slot_id = DoorSlot(),
  };
  target.param.key_offset = yard.AddString("target");
  target.param.type = data::pak::scripting::ScriptParamType::kNode;
  target.param.value.as_node = world::NodeRef { .node_index = garage };
  yard.AddComponents(
    ComponentType::kScriptParamOverride, std::vector { target });

  const auto instance = Run(yard.Build());
  auto root = instance.nodes.at(parked);
  const auto path = std::vector { holder_id, door_id };
  auto door = root.GetInstance().Find(path);
  auto nested = root.GetInstance().Find(std::span(path).first(1U));
  ASSERT_TRUE(door.has_value());
  ASSERT_TRUE(nested.has_value());
  auto shed = Scene().CreateNode("Shed");
  const auto garage_node = instance.nodes.at(garage);
  // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
  const auto built_target = TargetOf(*door);

  const auto inner
    = SetChange(*nested, Target({ door_id }, shed), Loader(), Scripts());
  const auto inner_target = TargetOf(*door);
  const auto cleared = ClearChange(root,
    data::ChangeAddress {
      .path = path,
      .target = data::ParameterAddress {
        .slot_id = DoorSlot(),
        .key = "target",
      },
    },
    Loader(), Scripts());

  EXPECT_EQ(built_target, garage_node.GetHandle());
  EXPECT_TRUE(inner.issues.empty());
  ASSERT_EQ(inner.scripts.size(), 1U);
  EXPECT_TRUE(inner.scripts.front().diagnostics.empty());
  EXPECT_EQ(inner_target, garage_node.GetHandle());
  EXPECT_TRUE(cleared.issues.empty());
  EXPECT_EQ(TargetOf(*door), shed.GetHandle());
  // NOLINTEND(bugprone-unchecked-optional-access)
}

} // namespace oxygen::instantiation::test
