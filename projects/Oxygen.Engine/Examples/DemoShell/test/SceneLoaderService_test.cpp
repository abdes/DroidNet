//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DemoShell/Services/SceneLoaderService.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/CollisionShapeData.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResidencyPin.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::testing {

namespace {
  namespace pakw = data::pak::world;

  // Values the descriptor builders write and the hydration tests read back.
  constexpr float kFogRadialExtinction = 0.3F;
  constexpr float kFogHeightExtinction = 0.2F;
  constexpr float kFogHeightFalloff = 0.15F;
  constexpr float kFogHeightOffset = 1.25F;
  constexpr float kFogPhaseG = 0.4F;
  constexpr auto kFogAlbedo = std::array { 0.7F, 0.8F, 0.9F };
  constexpr auto kFogEmissive = std::array { 0.1F, 0.2F, 0.3F };
  constexpr int32_t kFogSortPriority = 2;

  constexpr float kSunShadowBias = 0.0007F;
  constexpr float kSunShadowNormalBias = 0.03F;
  constexpr float kSunAngularSizeRadians = 0.00951F;
  constexpr auto kSunCascadeDistances
    = std::array { 250.0F, 900.0F, 2200.0F, 4200.0F };
  constexpr float kSunDistributionExponent = 2.5F;
  constexpr float kSunMaxShadowDistance = 4200.0F;
  constexpr float kSunTransitionFraction = 0.12F;
  constexpr float kSunFadeoutFraction = 0.18F;
  constexpr float kSunIntensityLux = 95000.0F;

  constexpr auto kViewportExtent = Extent<uint32_t> {
    .width = 1280U,
    .height = 720U,
  };
  constexpr auto kRuntimeSceneCapacity = 64U;

  //! A scene of `node_count` nodes, the first the root of the others.
  auto MinimalScene(const uint32_t node_count) -> cooker::testing::SceneBuilder
  {
    auto builder = cooker::testing::SceneBuilder { "Minimal" };
    const auto root = builder.Node("Root");
    for (uint32_t i = 1U; i < node_count; ++i) {
      builder.Node("Node" + std::to_string(i), root);
    }
    return builder;
  }

  auto SceneWithLocalFog() -> cooker::testing::SceneBuilder
  {
    auto builder = cooker::testing::SceneBuilder { "LocalFog" };
    const auto root = builder.Node("Root");
    const auto fog_node = builder.Node("FogNode", root);
    auto local_fog = cooker::testing::SceneBuilder::DefaultLocalFogVolume();
    local_fog.enabled = 1U;
    local_fog.radial_fog_extinction = kFogRadialExtinction;
    local_fog.height_fog_extinction = kFogHeightExtinction;
    local_fog.height_fog_falloff = kFogHeightFalloff;
    local_fog.height_fog_offset = kFogHeightOffset;
    local_fog.fog_phase_g = kFogPhaseG;
    std::ranges::copy(kFogAlbedo, std::begin(local_fog.fog_albedo));
    std::ranges::copy(kFogEmissive, std::begin(local_fog.fog_emissive));
    local_fog.sort_priority = kFogSortPriority;
    builder.LocalFogVolume(fog_node, local_fog);
    return builder;
  }

  auto SceneWithFlagModes() -> cooker::testing::SceneBuilder
  {
    auto builder = cooker::testing::SceneBuilder { "FlagModes" };
    const auto root = builder.Node("Root");
    const auto inherited = builder.Node("Inherited", root);
    const auto local = builder.Node("Local", root);
    builder.Flags(inherited,
      { .values = 0U, .inherited = pakw::kSceneNodeFlags_Inheritable });
    builder.Flags(local,
      {
        .values = pakw::kSceneNodeFlag_Visible,
        .inherited = pakw::kSceneNodeFlag_ReceivesShadows,
      });
    return builder;
  }

  auto SceneWithDirectionalLight() -> cooker::testing::SceneBuilder
  {
    auto builder = cooker::testing::SceneBuilder { "Sun" };
    const auto root = builder.Node("Root");
    const auto sun_node = builder.Node("SunNode", root);
    auto directional = cooker::testing::SceneBuilder::DefaultDirectionalLight();
    directional.common.casts_shadows = 1U;
    directional.common.shadow.bias = kSunShadowBias;
    directional.common.shadow.normal_bias = kSunShadowNormalBias;
    directional.angular_size_radians = kSunAngularSizeRadians;
    directional.atmosphere_light_slot = 1U;
    directional.cascade_count
      = static_cast<uint32_t>(kSunCascadeDistances.size());
    std::ranges::copy(
      kSunCascadeDistances, std::begin(directional.cascade_distances));
    directional.distribution_exponent = kSunDistributionExponent;
    directional.split_mode
      = static_cast<uint8_t>(scene::DirectionalCsmSplitMode::kManualDistances);
    directional.max_shadow_distance = kSunMaxShadowDistance;
    directional.transition_fraction = kSunTransitionFraction;
    directional.distance_fadeout_fraction = kSunFadeoutFraction;
    directional.intensity_lux = kSunIntensityLux;
    builder.DirectionalLight(sun_node, directional);
    return builder;
  }

  //! Assets a renderable material override scene refers to.
  struct RenderableOverrideKeys final {
    data::AssetKey geometry;
    data::AssetKey material;
  };

  auto SceneWithRenderableMaterialOverride(const RenderableOverrideKeys& keys,
    const data::GeometryAsset& geometry) -> cooker::testing::SceneBuilder
  {
    auto builder = cooker::testing::SceneBuilder { "MaterialOverride" };
    const auto root = builder.Node("Root");
    const auto mesh_node = builder.Node("MeshNode", root);
    builder.Renderable(mesh_node, keys.geometry);
    const auto& inventory = geometry.Surfaces();
    auto assignment = pakw::OverrideValue {};
    assignment.as_asset = keys.material;
    builder.Override(mesh_node,
      data::schema::OverrideId::kGeometrySurfaceMaterial, assignment,
      inventory.surfaces.at(0).surface_id, inventory.layout_revision);
    return builder;
  }

  auto BuildSingleSubmeshGeometry(
    std::shared_ptr<const data::MaterialAsset> mat)
    -> std::shared_ptr<data::GeometryAsset>
  {
    using data::GeometryAsset;
    using data::MeshBuilder;
    using data::Vertex;
    using data::pak::geometry::GeometryAssetDesc;

    std::vector<Vertex> vertices(3);
    vertices.at(0).position = { -1.0F, -1.0F, 0.0F };
    vertices.at(1).position = { 1.0F, -1.0F, 0.0F };
    vertices.at(2).position = { 0.0F, 1.0F, 0.0F };
    std::vector<std::uint32_t> indices { 0U, 1U, 2U };

    auto builder = MeshBuilder {};
    builder.WithVertices(vertices)
      .WithIndices(indices)
      .BeginSubMesh("surface", std::move(mat))
      .WithMeshView({
        .first_index = 0U,
        .index_count = static_cast<uint32_t>(indices.size()),
        .first_vertex = 0U,
        .vertex_count = static_cast<uint32_t>(vertices.size()),
      })
      .EndSubMesh();

    auto mesh = builder.Build();
    GeometryAssetDesc desc {};
    desc.lod_count = 1U;
    desc.bounding_box_min[0] = -1.0F;
    desc.bounding_box_min[1] = -1.0F;
    desc.bounding_box_min[2] = 0.0F;
    desc.bounding_box_max[0] = 1.0F;
    desc.bounding_box_max[1] = 1.0F;
    desc.bounding_box_max[2] = 0.0F;

    std::vector<std::shared_ptr<data::Mesh>> lods;
    lods.push_back(std::shared_ptr<data::Mesh>(std::move(mesh)));
    return std::make_shared<GeometryAsset>(
      data::AssetKey {}, desc, std::move(lods));
  }

  auto BuildTestMaterial(const data::AssetKey& key)
    -> std::shared_ptr<data::MaterialAsset>
  {
    auto desc = data::pak::render::MaterialAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    return std::make_shared<data::MaterialAsset>(key, desc);
  }

  auto FindNodeByName(scene::Scene& scene, std::string_view name)
    -> std::optional<scene::SceneNode>
  {
    auto roots = scene.GetRootNodes();
    std::vector<scene::SceneNode> stack {};
    stack.reserve(roots.size());
    for (auto& root : roots) {
      stack.push_back(root);
    }

    while (!stack.empty()) {
      auto node = stack.back();
      stack.pop_back();
      if (!node.IsAlive()) {
        continue;
      }
      if (node.GetName() == name) {
        return node;
      }
      auto child = node.GetFirstChild();
      while (child.has_value()) {
        stack.push_back(*child);
        child = child->GetNextSibling();
      }
    }

    return std::nullopt;
  }

  class SceneLoaderTestAssetLoader final : public content::IAssetLoader {
  public:
    SceneLoaderTestAssetLoader() = default;
    ~SceneLoaderTestAssetLoader() override = default;

    OXYGEN_MAKE_NON_COPYABLE(SceneLoaderTestAssetLoader)
    OXYGEN_DEFAULT_MOVABLE(SceneLoaderTestAssetLoader)

    auto PutScene(const data::AssetKey& key,
      std::shared_ptr<data::SceneAsset> scene) -> void
    {
      scenes_.insert_or_assign(key, std::move(scene));
    }

    auto PutPhysicsSidecar(const data::AssetKey& scene_key,
      const data::AssetKey& sidecar_key,
      std::shared_ptr<data::PhysicsSceneAsset> sidecar) -> void
    {
      sidecar_keys_by_scene_.insert_or_assign(scene_key, sidecar_key);
      sidecars_.insert_or_assign(sidecar_key, std::move(sidecar));
    }

    auto PutGeometry(const data::AssetKey& key,
      std::shared_ptr<data::GeometryAsset> geometry) -> void
    {
      geometries_.insert_or_assign(key, std::move(geometry));
    }

    auto PutMaterial(const data::AssetKey& key,
      std::shared_ptr<data::MaterialAsset> material) -> void
    {
      materials_.insert_or_assign(key, std::move(material));
    }

    void StartLoadMaterialAsset(const data::AssetKey& key,
      data::SourceKey source, const content::LoadRequest& /*request*/,
      MaterialCallback on_complete) override
    {
      const auto asset = GetMaterialAsset(key);
      on_complete(asset && asset->GetSourceKey() == source ? asset : nullptr);
    }

    void StartLoadGeometryAsset(const data::AssetKey& key,
      data::SourceKey source, const content::LoadRequest& /*request*/,
      GeometryCallback on_complete) override
    {
      const auto asset = GetGeometryAsset(key);
      on_complete(asset && asset->GetSourceKey() == source ? asset : nullptr);
    }

    void StartLoadScene(const data::AssetKey& key, data::SourceKey source,
      const content::LoadRequest& /*request*/,
      SceneCallback on_complete) override
    {
      const auto found = scenes_.find(key);
      on_complete(
        found != scenes_.end() && found->second->GetSourceKey() == source
          ? found->second
          : nullptr);
    }

    void StartLoadScriptAsset(const data::AssetKey& /*key*/,
      data::SourceKey /*source*/, const content::LoadRequest& /*request*/,
      ScriptCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadPhysicsSceneAsset(const data::AssetKey& key,
      data::SourceKey source, const content::LoadRequest& /*request*/,
      PhysicsSceneCallback on_complete) override
    {
      const auto asset = GetPhysicsSceneAsset(key);
      on_complete(asset && asset->GetSourceKey() == source ? asset : nullptr);
    }

    [[nodiscard]] auto GetMaterialAsset(
      const data::AssetKey& key, const data::Asset& /*context_asset*/)
      -> std::shared_ptr<data::MaterialAsset> override
    {
      return GetMaterialAsset(key);
    }

    [[nodiscard]] auto GetGeometryAsset(
      const data::AssetKey& key, const data::Asset& /*context_asset*/)
      -> std::shared_ptr<data::GeometryAsset> override
    {
      return GetGeometryAsset(key);
    }

    [[nodiscard]] auto GetScriptAsset(
      const data::AssetKey& /*key*/, const data::Asset& /*context_asset*/)
      -> std::shared_ptr<data::ScriptAsset> override
    {
      return nullptr;
    }

    void StartLoadTexture(
      content::ResourceKey /*key*/, TextureCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadTexture(
      content::CookedResourceData<data::TextureResource> /*cooked*/,
      TextureCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadBuffer(
      content::ResourceKey /*key*/, BufferCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadBuffer(
      content::CookedResourceData<data::BufferResource> /*cooked*/,
      BufferCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadMaterialAsset(
      const data::AssetKey& key, MaterialCallback on_complete) override
    {
      const auto it = materials_.find(key);
      on_complete(it == materials_.end() ? nullptr : it->second);
    }

    void StartLoadGeometryAsset(
      const data::AssetKey& key, GeometryCallback on_complete) override
    {
      const auto it = geometries_.find(key);
      on_complete(it == geometries_.end() ? nullptr : it->second);
    }

    void StartLoadScene(
      const data::AssetKey& key, SceneCallback on_complete) override
    {
      const auto it = scenes_.find(key);
      on_complete(it == scenes_.end() ? nullptr : it->second);
    }

    void StartLoadScriptAsset(
      const data::AssetKey& /*key*/, ScriptCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadInputMappingContextAsset(const data::AssetKey& /*key*/,
      InputMappingContextCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadPhysicsSceneAsset(
      const data::AssetKey& key, PhysicsSceneCallback on_complete) override
    {
      const auto it = sidecars_.find(key);
      on_complete(it == sidecars_.end() ? nullptr : it->second);
    }

    void StartLoadPhysicsResource(content::ResourceKey /*key*/,
      PhysicsResourceCallback on_complete) override
    {
      on_complete(nullptr);
    }

    auto AddPakFile(const std::filesystem::path& /*path*/) -> void override { }
    auto AddLooseCookedRoot(const std::filesystem::path& /*path*/)
      -> void override
    {
    }
    auto DoMountLooseCookedGeneration(const std::filesystem::path& /*path*/,
      std::optional<data::SourceKey> /*replaces*/) -> data::SourceKey override
    {
      throw std::logic_error(
        "Scene hydration test loader does not mount disk generations");
    }
    auto RetireLooseCookedGeneration(data::SourceKey /*source_key*/)
      -> bool override
    {
      return false;
    }

    [[nodiscard]] auto UnmetRequirements(data::SourceKey /*source_key*/) const
      -> std::vector<content::FailedRequirement> override
    {
      return {};
    }
    auto ClearMounts() -> void override { }
    auto WaitForPendingLoadsAsync() -> co::Co<> override { co_return; }
    auto PrepareMountsAsync(std::vector<content::ContentMount> /*mounts*/)
      -> co::Co<content::PreparedMountSet> override
    {
      throw std::logic_error(
        "Scene hydration test loader does not prepare disk mounts");
    }
    auto CommitPreparedMounts(content::PreparedMountSet&& prepared)
      -> content::MountRetirement override
    {
      [[maybe_unused]] const auto discarded = std::move(prepared);
      throw std::logic_error(
        "Scene hydration test loader does not commit disk mounts");
    }
    auto TrimCache() -> void override { }
    auto ProcessPendingReleases() -> void override { }

    auto SetResidencyPolicy(const content::ResidencyPolicy& policy)
      -> void override
    {
      residency_policy_ = policy;
    }
    [[nodiscard]] auto GetResidencyPolicy() const noexcept
      -> content::ResidencyPolicy override
    {
      return residency_policy_;
    }
    [[nodiscard]] auto QueryResidencyPolicyState() const
      -> content::ResidencyPolicyState override
    {
      return content::ResidencyPolicyState { .policy = residency_policy_ };
    }
    [[nodiscard]] auto EnumerateMountedScenes() const
      -> std::vector<MountedSceneEntry> override
    {
      return {};
    }

    [[nodiscard]] auto ReadMountedSceneHierarchyAsync(data::AssetKey /*key*/)
      -> co::Co<std::optional<SceneHierarchy>> override
    {
      co_return std::nullopt;
    }
    [[nodiscard]] auto ReadMountedSceneNodeAsync(
      data::AssetKey /*key*/, std::vector<data::NodeId> /*path*/)
      -> co::Co<std::optional<SceneNodeValues>> override
    {
      co_return std::nullopt;
    }
    auto StartReadMountedSceneHierarchy(const data::AssetKey& /*key*/,
      SceneHierarchyCallback on_complete) -> void override
    {
      on_complete(std::nullopt);
    }
    auto StartReadMountedSceneNode(const data::AssetKey& /*key*/,
      std::vector<data::NodeId> /*path*/, SceneNodeValuesCallback on_complete)
      -> void override
    {
      on_complete(std::nullopt);
    }
    [[nodiscard]] auto EnumerateMountedInputContextsAsync()
      -> co::Co<std::vector<MountedInputContextEntry>> override
    {
      co_return std::vector<MountedInputContextEntry> {};
    }
    [[nodiscard]] auto EnumerateMountedSources() const
      -> std::vector<MountedSourceEntry> override
    {
      return {};
    }

    auto RegisterConsoleBindings(
      observer_ptr<console::Console> /*console*/) noexcept -> void override
    {
    }
    auto ApplyConsoleCVars(const console::Console& /*console*/) -> void override
    {
    }

    [[nodiscard]] auto PeekTexture(content::ResourceKey /*key*/) const
      -> observer_ptr<const data::TextureResource> override
    {
      return {};
    }

    [[nodiscard]] auto GetTexture(content::ResourceKey /*key*/)
      -> std::shared_ptr<data::TextureResource> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetBuffer(content::ResourceKey /*key*/)
      -> std::shared_ptr<data::BufferResource> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetMaterialAsset(const data::AssetKey& key)
      -> std::shared_ptr<data::MaterialAsset> override
    {
      const auto it = materials_.find(key);
      return it == materials_.end() ? nullptr : it->second;
    }
    [[nodiscard]] auto GetGeometryAsset(const data::AssetKey& key)
      -> std::shared_ptr<data::GeometryAsset> override
    {
      const auto it = geometries_.find(key);
      return it == geometries_.end() ? nullptr : it->second;
    }
    [[nodiscard]] auto GetScriptAsset(const data::AssetKey& /*key*/)
      -> std::shared_ptr<data::ScriptAsset> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetScriptResource(content::ResourceKey /*key*/)
      -> std::shared_ptr<data::ScriptResource> override
    {
      return nullptr;
    }
    auto LoadScriptResourceAsync(content::ResourceKey /*key*/)
      -> co::Co<std::shared_ptr<data::ScriptResource>> override
    {
      co_return nullptr;
    }
    [[nodiscard]] auto ResolveTextureResourceKey(
      std::string_view /*virtual_path*/)
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }

    auto MakeTextureResourceKey(oxygen::data::SourceKey /*source_key*/,
      oxygen::data::pak::core::ResourceIndexT /*resource_index*/)
      -> std::optional<oxygen::content::ResourceKey> override
    {
      return std::nullopt;
    }

    [[nodiscard]] auto MakeTextureResourceKeyForAsset(
      const data::Asset& /*context_asset*/,
      data::ResourceReferenceIndex /*resource_index*/)
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }

    [[nodiscard]] auto MakeScriptResourceKeyForAsset(
      const data::Asset& /*context_asset*/,
      data::ResourceReferenceIndex /*resource_index*/)
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }

    [[nodiscard]] auto GetPhysicsSceneAsset(const data::AssetKey& key)
      -> std::shared_ptr<data::PhysicsSceneAsset> override
    {
      const auto it = sidecars_.find(key);
      return it == sidecars_.end() ? nullptr : it->second;
    }
    [[nodiscard]] auto GetPhysicsResource(content::ResourceKey /*key*/)
      -> std::shared_ptr<data::PhysicsResource> override
    {
      return nullptr;
    }
    auto LoadPhysicsResourceAsync(content::ResourceKey /*key*/)
      -> co::Co<std::shared_ptr<data::PhysicsResource>> override
    {
      co_return nullptr;
    }
    [[nodiscard]] auto MakePhysicsResourceKey(data::SourceKey /*source_key*/,
      data::pak::core::ResourceIndexT /*resource_index*/)
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
      const data::Asset& /*context_asset*/,
      data::pak::core::ResourceIndexT /*resource_index*/)
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
      const data::Asset& /*context_asset*/,
      const data::AssetKey& /*resource_asset_key*/)
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto ReadCollisionShapeForAsset(
      const data::Asset& /*context_asset*/,
      const data::AssetKey& /*shape_asset_key*/) const
      -> std::optional<content::CollisionShapeData> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto ReadPhysicsMaterialAssetDescForAsset(
      const data::Asset& /*context_asset*/,
      const data::AssetKey& /*material_asset_key*/) const
      -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc> override
    {
      return std::nullopt;
    }

    [[nodiscard]] auto GetInputActionAsset(
      const data::AssetKey& key, const data::Asset& /*context_asset*/)
      -> std::shared_ptr<data::InputActionAsset> override
    {
      return GetInputActionAsset(key);
    }

    [[nodiscard]] auto GetInputActionAsset(const data::AssetKey& /*key*/)
      -> std::shared_ptr<data::InputActionAsset> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetInputMappingContextAsset(
      const data::AssetKey& /*key*/)
      -> std::shared_ptr<data::InputMappingContextAsset> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetHydratedScriptSlots(
      const data::SceneAsset& /*scene_asset*/,
      const data::pak::scripting::ScriptingComponentRecord& /*component*/) const
      -> std::vector<HydratedScriptSlot> override
    {
      return {};
    }
    [[nodiscard]] auto FindPhysicsSidecarAssetKeyForScene(
      const data::Asset& scene) const -> std::optional<data::AssetKey> override
    {
      const auto it = sidecar_keys_by_scene_.find(scene.GetAssetKey());
      return it == sidecar_keys_by_scene_.end()
        ? std::nullopt
        : std::optional<data::AssetKey> { it->second };
    }

    [[nodiscard]] auto HasTexture(content::ResourceKey /*key*/) const noexcept
      -> bool override
    {
      return false;
    }
    [[nodiscard]] auto HasBuffer(content::ResourceKey /*key*/) const noexcept
      -> bool override
    {
      return false;
    }
    [[nodiscard]] auto HasMaterialAsset(
      const data::AssetKey& key) const noexcept -> bool override
    {
      return materials_.contains(key);
    }
    [[nodiscard]] auto HasGeometryAsset(
      const data::AssetKey& key) const noexcept -> bool override
    {
      return geometries_.contains(key);
    }
    [[nodiscard]] auto HasScriptAsset(
      const data::AssetKey& /*key*/) const noexcept -> bool override
    {
      return false;
    }
    [[nodiscard]] auto HasPhysicsSceneAsset(
      const data::AssetKey& key) const noexcept -> bool override
    {
      return sidecars_.contains(key);
    }
    [[nodiscard]] auto HasInputActionAsset(
      const data::AssetKey& /*key*/) const noexcept -> bool override
    {
      return false;
    }
    [[nodiscard]] auto HasInputMappingContextAsset(
      const data::AssetKey& /*key*/) const noexcept -> bool override
    {
      return false;
    }

    auto PinResource(content::ResourceKey /*key*/)
      -> content::ResidencyPin override
    {
      return {};
    }

    auto PinAsset(const data::AssetKey& /*key*/)
      -> content::ResidencyPin override
    {
      return {};
    }

    auto SubscribeResourceEvictions(TypeId resource_type,
      EvictionHandler handler) -> EvictionSubscription override
    {
      const content::EvictionSubscriptionId id { next_subscription_id_++ };
      eviction_handlers_[resource_type].insert_or_assign(
        id, std::move(handler));
      return MakeEvictionSubscription(resource_type, id,
        observer_ptr<IAssetLoader> { this }, eviction_alive_token_);
    }

    [[nodiscard]] auto MintSyntheticTextureKey()
      -> content::ResourceKey override
    {
      return content::ResourceKey { next_resource_key_++ };
    }
    [[nodiscard]] auto MintSyntheticBufferKey() -> content::ResourceKey override
    {
      return content::ResourceKey { next_resource_key_++ };
    }

    [[nodiscard]] auto MintSyntheticScriptKey() -> content::ResourceKey override
    {
      return content::ResourceKey { next_resource_key_++ };
    }

  private:
    void UnsubscribeResourceEvictions(TypeId resource_type,
      const content::EvictionSubscriptionId id) noexcept override
    {
      const auto it = eviction_handlers_.find(resource_type);
      if (it == eviction_handlers_.end()) {
        return;
      }
      it->second.erase(id);
    }

    content::ResidencyPolicy residency_policy_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::SceneAsset>>
      scenes_;
    std::unordered_map<data::AssetKey, data::AssetKey> sidecar_keys_by_scene_;
    std::unordered_map<data::AssetKey, std::shared_ptr<data::PhysicsSceneAsset>>
      sidecars_;
    std::unordered_map<data::AssetKey, std::shared_ptr<data::GeometryAsset>>
      geometries_;
    std::unordered_map<data::AssetKey, std::shared_ptr<data::MaterialAsset>>
      materials_;
    std::unordered_map<TypeId,
      std::unordered_map<content::EvictionSubscriptionId, EvictionHandler>>
      eviction_handlers_;
    uint64_t next_subscription_id_ { 1U };
    std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };
    uint64_t next_resource_key_ { 1U };
  };

  auto MakeService(content::IAssetLoader& loader)
    -> std::shared_ptr<SceneLoaderService>
  {
    return std::make_shared<SceneLoaderService>(loader, kViewportExtent,
      std::filesystem::path {}, nullptr, nullptr, nullptr,
      PathFinder(std::make_shared<const PathFinderConfig>(),
        std::filesystem::current_path()));
  }

  auto BuildScene(SceneLoaderService& service, scene::Scene& runtime_scene,
    std::shared_ptr<const data::SceneAsset> asset) -> void
  {
    co::testing::TestEventLoop loop;
    co::Run(loop, service.BuildSceneAsync(runtime_scene, std::move(asset)));
  }

  auto SceneKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/Game/Tests/SceneLoader/a.oscene");
  }

  auto SidecarKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/Game/Tests/SceneLoader/a.opscene");
  }

  //! What a test sidecar says about the scene it belongs to; the defaults
  //! describe the one-node scene `StartLoadWithSidecar` loads.
  struct SidecarTarget final {
    data::AssetKey scene_key = SceneKey();
    uint32_t node_count = 1U;
    bool matches_content = true;
  };

  //! Starts loading a one-node scene whose physics sidecar names `target`.
  auto StartLoadWithSidecar(SceneLoaderTestAssetLoader& loader,
    const SidecarTarget& target) -> std::shared_ptr<SceneLoaderService>
  {
    using cooker::testing::DescriptorDefects;
    auto scene = MinimalScene(1U).Build(SceneKey());
    auto sidecar = cooker::testing::PhysicsSidecarBuilder {}.Bytes(
      SceneKey(), scene->GetRawData(), "PhysicsScene");
    if (!target.matches_content) {
      DescriptorDefects::CorruptSidecarSceneLayout(sidecar);
    }
    if (target.scene_key != SceneKey()) {
      DescriptorDefects::SetSidecarScene(sidecar, target.scene_key);
    }
    if (target.node_count != 1U) {
      DescriptorDefects::SetSidecarNodeCount(sidecar, target.node_count);
    }
    loader.PutScene(SceneKey(), scene);
    loader.PutPhysicsSidecar(SceneKey(), SidecarKey(),
      std::make_shared<data::PhysicsSceneAsset>(
        SidecarKey(), std::move(sidecar)));
    auto service = MakeService(loader);
    service->StartLoad(SceneKey());
    return service;
  }
} // namespace

//=== Loading a scene and its physics sidecar ===----------------------------//

//! A scene without a physics sidecar loads on its own.
NOLINT_TEST(SceneLoaderServiceLoadTest, LoadsASceneThatHasNoSidecar)
{
  auto loader = SceneLoaderTestAssetLoader {};
  loader.PutScene(SceneKey(), MinimalScene(1U).Build(SceneKey()));
  auto service = MakeService(loader);

  service->StartLoad(SceneKey());

  ASSERT_TRUE(service->IsReady());
  const auto result = service->GetResult();
  EXPECT_THAT(result.asset, ::testing::NotNull());
  EXPECT_THAT(result.physics_asset, ::testing::IsNull());
}

//! A sidecar cooked for this scene's key, content and node count loads with
//! it.
NOLINT_TEST(SceneLoaderServiceLoadTest, LoadsASceneWithItsSidecar)
{
  auto loader = SceneLoaderTestAssetLoader {};

  const auto service = StartLoadWithSidecar(loader, {});

  ASSERT_TRUE(service->IsReady());
  EXPECT_FALSE(service->IsFailed());
  const auto result = service->GetResult();
  EXPECT_EQ(result.scene_key, SceneKey());
  EXPECT_THAT(result.asset, ::testing::NotNull());
  EXPECT_THAT(result.physics_asset, ::testing::NotNull());
}

//! A sidecar cooked for other content of the scene fails the load.
NOLINT_TEST(SceneLoaderServiceLoadTest, RejectsASidecarOfOtherSceneContent)
{
  auto loader = SceneLoaderTestAssetLoader {};

  const auto service
    = StartLoadWithSidecar(loader, { .matches_content = false });

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

//! A sidecar that names another scene fails the load.
NOLINT_TEST(SceneLoaderServiceLoadTest, RejectsASidecarOfAnotherScene)
{
  auto loader = SceneLoaderTestAssetLoader {};

  const auto service = StartLoadWithSidecar(loader,
    {
      .scene_key
      = data::AssetKey::FromVirtualPath("/Game/Tests/SceneLoader/b.oscene"),
    });

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

//! A sidecar cooked for another node count fails the load.
NOLINT_TEST(SceneLoaderServiceLoadTest, RejectsASidecarOfAnotherNodeCount)
{
  auto loader = SceneLoaderTestAssetLoader {};

  const auto service = StartLoadWithSidecar(loader, { .node_count = 2U });

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

//=== Building the runtime scene ===-----------------------------------------//

//! Each node flag keeps its cooked source mode: an inherited flag follows its
//! parent, a local one keeps its own value.
NOLINT_TEST(SceneLoaderServiceBuildTest, KeepsEachNodeFlagSourceMode)
{
  using scene::SceneNodeFlags;
  constexpr auto kInheritable = std::array {
    SceneNodeFlags::kVisible,
    SceneNodeFlags::kCastsShadows,
    SceneNodeFlags::kReceivesShadows,
  };
  auto loader = SceneLoaderTestAssetLoader {};
  const auto asset = SceneWithFlagModes().Build(SceneKey());
  loader.PutScene(SceneKey(), asset);
  auto service = MakeService(loader);
  auto runtime_scene
    = std::make_shared<scene::Scene>("FlagsHydration", kRuntimeSceneCapacity);

  BuildScene(*service, *runtime_scene, asset);
  runtime_scene->Update();

  auto root = FindNodeByName(*runtime_scene, "Root");
  auto inherited = FindNodeByName(*runtime_scene, "Inherited");
  auto local = FindNodeByName(*runtime_scene, "Local");
  if (!root.has_value() || !inherited.has_value() || !local.has_value()) {
    FAIL() << "the built scene lacks a cooked node";
  }
  const auto root_flags = root->GetFlags();
  const auto inherited_flags = inherited->GetFlags();
  const auto local_flags = local->GetFlags();
  if (!root_flags.has_value() || !inherited_flags.has_value()
    || !local_flags.has_value()) {
    FAIL() << "a built node has no flags";
  }
  for (const auto flag : kInheritable) {
    EXPECT_TRUE(inherited_flags->get().IsInherited(flag));
    EXPECT_FALSE(inherited_flags->get().GetEffectiveValue(flag));
  }
  EXPECT_FALSE(local_flags->get().IsInherited(SceneNodeFlags::kVisible));
  EXPECT_TRUE(local_flags->get().GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_FALSE(local_flags->get().IsInherited(SceneNodeFlags::kCastsShadows));
  EXPECT_FALSE(
    local_flags->get().GetEffectiveValue(SceneNodeFlags::kCastsShadows));
  EXPECT_TRUE(local_flags->get().IsInherited(SceneNodeFlags::kReceivesShadows));

  for (const auto flag : kInheritable) {
    root_flags->get().SetLocalValue(flag, true);
  }
  runtime_scene->Update();

  for (const auto flag : kInheritable) {
    EXPECT_TRUE(inherited_flags->get().GetEffectiveValue(flag));
  }
  EXPECT_FALSE(
    local_flags->get().GetEffectiveValue(SceneNodeFlags::kCastsShadows));
  EXPECT_TRUE(
    local_flags->get().GetEffectiveValue(SceneNodeFlags::kReceivesShadows));
}

//! A directional light gets its cooked shadow, atmosphere and cascade
//! settings.
NOLINT_TEST(SceneLoaderServiceBuildTest, BuildsADirectionalLightFromItsRecord)
{
  auto loader = SceneLoaderTestAssetLoader {};
  auto scene_asset = SceneWithDirectionalLight().Build(SceneKey());
  loader.PutScene(SceneKey(), scene_asset);
  auto service = MakeService(loader);
  auto runtime_scene = std::make_shared<scene::Scene>(
    "DemoShell.DirectionalCsmHydration", kRuntimeSceneCapacity);

  BuildScene(*service, *runtime_scene, scene_asset);

  auto sun_node = FindNodeByName(*runtime_scene, "SunNode");
  if (!sun_node.has_value()) {
    FAIL() << "the built scene lacks the sun node";
  }
  auto light = sun_node->GetLightAs<scene::DirectionalLight>();
  if (!light.has_value()) {
    FAIL() << "the sun node has no directional light";
  }
  const auto& sun = light->get();
  EXPECT_TRUE(sun.Common().casts_shadows);
  EXPECT_FLOAT_EQ(sun.Common().shadow.bias, kSunShadowBias);
  EXPECT_FLOAT_EQ(sun.Common().shadow.normal_bias, kSunShadowNormalBias);
  EXPECT_EQ(sun.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kPrimary);
  EXPECT_FLOAT_EQ(sun.GetIntensityLux(), kSunIntensityLux);
  const auto& csm = sun.CascadedShadows();
  EXPECT_EQ(csm.cascade_count, kSunCascadeDistances.size());
  EXPECT_EQ(csm.split_mode, scene::DirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(csm.max_shadow_distance, kSunMaxShadowDistance);
  EXPECT_THAT(csm.cascade_distances,
    ::testing::Pointwise(::testing::FloatEq(), kSunCascadeDistances));
  EXPECT_FLOAT_EQ(csm.distribution_exponent, kSunDistributionExponent);
  EXPECT_FLOAT_EQ(csm.transition_fraction, kSunTransitionFraction);
  EXPECT_FLOAT_EQ(csm.distance_fadeout_fraction, kSunFadeoutFraction);
}

//! A local fog volume gets every cooked fog value.
NOLINT_TEST(SceneLoaderServiceBuildTest, BuildsALocalFogVolumeFromItsRecord)
{
  auto loader = SceneLoaderTestAssetLoader {};
  auto scene_asset = SceneWithLocalFog().Build(SceneKey());
  loader.PutScene(SceneKey(), scene_asset);
  auto service = MakeService(loader);
  auto runtime_scene = std::make_shared<scene::Scene>(
    "DemoShell.LocalFogHydration", kRuntimeSceneCapacity);

  BuildScene(*service, *runtime_scene, scene_asset);

  auto fog_node = FindNodeByName(*runtime_scene, "FogNode");
  if (!fog_node.has_value()) {
    FAIL() << "the built scene lacks the fog node";
  }
  const auto impl = fog_node->GetImpl();
  if (!impl.has_value()) {
    FAIL() << "the fog node has no implementation";
  }
  ASSERT_TRUE(impl->get().HasComponent<scene::environment::LocalFogVolume>());
  const auto& local_fog
    = impl->get().GetComponent<scene::environment::LocalFogVolume>();
  EXPECT_TRUE(local_fog.IsEnabled());
  EXPECT_FLOAT_EQ(local_fog.GetRadialFogExtinction(), kFogRadialExtinction);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogExtinction(), kFogHeightExtinction);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogFalloff(), kFogHeightFalloff);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogOffset(), kFogHeightOffset);
  EXPECT_FLOAT_EQ(local_fog.GetFogPhaseG(), kFogPhaseG);
  EXPECT_EQ(local_fog.GetFogAlbedo(),
    glm::vec3(kFogAlbedo.at(0), kFogAlbedo.at(1), kFogAlbedo.at(2)));
  EXPECT_EQ(local_fog.GetFogEmissive(),
    glm::vec3(kFogEmissive.at(0), kFogEmissive.at(1), kFogEmissive.at(2)));
  EXPECT_EQ(local_fog.GetSortPriority(), kFogSortPriority);
}

//! A surface material override replaces the geometry's own material.
NOLINT_TEST(SceneLoaderServiceBuildTest, AppliesASurfaceMaterialOverride)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto geometry_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/SceneLoader/cube.ogeo");
  const auto material_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/SceneLoader/override.omat");
  auto override_material = BuildTestMaterial(material_key);
  auto geometry
    = BuildSingleSubmeshGeometry(data::MaterialAsset::CreateDefault());
  auto scene_asset = SceneWithRenderableMaterialOverride(
    { .geometry = geometry_key, .material = material_key }, *geometry)
                       .Build(SceneKey());
  loader.PutScene(SceneKey(), scene_asset);
  loader.PutGeometry(geometry_key, geometry);
  loader.PutMaterial(material_key, override_material);
  auto service = MakeService(loader);
  auto runtime_scene = std::make_shared<scene::Scene>(
    "DemoShell.RenderableOverride", kRuntimeSceneCapacity);

  BuildScene(*service, *runtime_scene, scene_asset);

  auto mesh_node = FindNodeByName(*runtime_scene, "MeshNode");
  if (!mesh_node.has_value()) {
    FAIL() << "the built scene lacks the mesh node";
  }
  auto renderable = mesh_node->GetRenderable();
  ASSERT_TRUE(renderable.HasGeometry());
  EXPECT_EQ(renderable.ResolveSubmeshMaterial(
              data::LodIndex { 0U }, data::SubmeshIndex { 0U }),
    override_material);
}

} // namespace oxygen::examples::testing
