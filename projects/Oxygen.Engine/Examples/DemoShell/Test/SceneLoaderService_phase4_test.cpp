//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "DemoShell/Services/SceneLoaderService.h"
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::examples::testing {

namespace {
  namespace pakw = data::pak::world;
  namespace pakp = data::pak::physics;

  auto BuildMinimalSceneDescriptorBytes(const uint32_t node_count)
    -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes.offset = static_cast<data::pak::core::OffsetT>(sizeof(desc));
    desc.nodes.count = node_count;
    desc.nodes.entry_size = sizeof(pakw::NodeRecord);
    const auto nodes_bytes
      = static_cast<size_t>(node_count) * sizeof(pakw::NodeRecord);
    desc.scene_strings.offset
      = static_cast<data::pak::core::StringTableOffsetT>(
        desc.nodes.offset + static_cast<data::pak::core::OffsetT>(nodes_bytes));
    desc.scene_strings.size = 1;

    auto bytes = std::vector<std::byte> {};
    bytes.resize(
      static_cast<size_t>(desc.scene_strings.offset + desc.scene_strings.size),
      std::byte { 0 });
    std::memcpy(bytes.data(), &desc, sizeof(desc));

    for (uint32_t i = 0; i < node_count; ++i) {
      auto node = pakw::NodeRecord {};
      node.parent_index = (i == 0U) ? 0U : 0U;
      node.scene_name_offset = 0U;
      const auto node_offset
        = static_cast<size_t>(desc.nodes.offset) + i * sizeof(node);
      std::memcpy(bytes.data() + node_offset, &node, sizeof(node));
    }

    bytes[desc.scene_strings.offset] = std::byte { 0 };
    return bytes;
  }

  auto BuildSceneDescriptorBytesWithLocalFog() -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes.offset = static_cast<data::pak::core::OffsetT>(sizeof(desc));
    desc.nodes.count = 2U;
    desc.nodes.entry_size = sizeof(pakw::NodeRecord);

    auto root = pakw::NodeRecord {};
    root.parent_index = 0U;
    root.scene_name_offset = 1U;

    auto fog_node = pakw::NodeRecord {};
    fog_node.parent_index = 0U;
    fog_node.scene_name_offset = 6U;

    const auto strings = std::string("\0Root\0FogNode\0", 14);
    const auto nodes_bytes = sizeof(root) + sizeof(fog_node);
    desc.scene_strings.offset = static_cast<data::pak::core::StringTableOffsetT>(
      desc.nodes.offset + nodes_bytes);
    desc.scene_strings.size
      = static_cast<data::pak::core::StringTableSizeT>(strings.size());
    desc.component_table_directory_offset
      = desc.scene_strings.offset + desc.scene_strings.size;
    desc.component_table_count = 1U;

    auto table_desc = pakw::SceneComponentTableDesc {};
    table_desc.component_type
      = static_cast<uint32_t>(data::ComponentType::kLocalFogVolume);
    table_desc.table.offset = desc.component_table_directory_offset
      + static_cast<data::pak::core::OffsetT>(sizeof(table_desc));
    table_desc.table.count = 1U;
    table_desc.table.entry_size = sizeof(pakw::LocalFogVolumeRecord);

    auto local_fog = pakw::LocalFogVolumeRecord {};
    local_fog.node_index = 1U;
    local_fog.enabled = 1U;
    local_fog.radial_fog_extinction = 0.3F;
    local_fog.height_fog_extinction = 0.2F;
    local_fog.height_fog_falloff = 0.15F;
    local_fog.height_fog_offset = 1.25F;
    local_fog.fog_phase_g = 0.4F;
    local_fog.fog_albedo[0] = 0.7F;
    local_fog.fog_albedo[1] = 0.8F;
    local_fog.fog_albedo[2] = 0.9F;
    local_fog.fog_emissive[0] = 0.1F;
    local_fog.fog_emissive[1] = 0.2F;
    local_fog.fog_emissive[2] = 0.3F;
    local_fog.sort_priority = 2;

    auto bytes = std::vector<std::byte> {};
    bytes.resize(sizeof(desc) + nodes_bytes + strings.size()
      + sizeof(table_desc) + sizeof(local_fog));

    auto* cursor = bytes.data();
    std::memcpy(cursor, &desc, sizeof(desc));
    cursor += sizeof(desc);
    std::memcpy(cursor, &root, sizeof(root));
    cursor += sizeof(root);
    std::memcpy(cursor, &fog_node, sizeof(fog_node));
    cursor += sizeof(fog_node);
    std::memcpy(cursor, strings.data(), strings.size());
    cursor += strings.size();
    std::memcpy(cursor, &table_desc, sizeof(table_desc));
    cursor += sizeof(table_desc);
    std::memcpy(cursor, &local_fog, sizeof(local_fog));
    return bytes;
  }

  auto BuildSceneDescriptorBytesWithDirectionalLight()
    -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes.offset = static_cast<data::pak::core::OffsetT>(sizeof(desc));
    desc.nodes.count = 2U;
    desc.nodes.entry_size = sizeof(pakw::NodeRecord);

    auto root = pakw::NodeRecord {};
    root.parent_index = 0U;
    root.scene_name_offset = 1U;

    auto sun_node = pakw::NodeRecord {};
    sun_node.parent_index = 0U;
    sun_node.scene_name_offset = 6U;

    const auto strings = std::string("\0Root\0SunNode\0", 14);
    const auto nodes_bytes = sizeof(root) + sizeof(sun_node);
    desc.scene_strings.offset = static_cast<data::pak::core::StringTableOffsetT>(
      desc.nodes.offset + nodes_bytes);
    desc.scene_strings.size
      = static_cast<data::pak::core::StringTableSizeT>(strings.size());
    desc.component_table_directory_offset
      = desc.scene_strings.offset + desc.scene_strings.size;
    desc.component_table_count = 1U;

    auto table_desc = pakw::SceneComponentTableDesc {};
    table_desc.component_type
      = static_cast<uint32_t>(data::ComponentType::kDirectionalLight);
    table_desc.table.offset = desc.component_table_directory_offset
      + static_cast<data::pak::core::OffsetT>(sizeof(table_desc));
    table_desc.table.count = 1U;
    table_desc.table.entry_size = sizeof(pakw::DirectionalLightRecord);

    auto directional = pakw::DirectionalLightRecord {};
    directional.node_index = 1U;
    directional.common.casts_shadows = 1U;
    directional.common.shadow.bias = 0.0007F;
    directional.common.shadow.normal_bias = 0.03F;
    directional.angular_size_radians = 0.00951F;
    directional.environment_contribution = 1U;
    directional.is_sun_light = 1U;
    directional.cascade_count = 4U;
    directional.cascade_distances[0] = 250.0F;
    directional.cascade_distances[1] = 900.0F;
    directional.cascade_distances[2] = 2200.0F;
    directional.cascade_distances[3] = 4200.0F;
    directional.distribution_exponent = 2.5F;
    directional.split_mode = static_cast<uint8_t>(
      scene::DirectionalCsmSplitMode::kManualDistances);
    directional.max_shadow_distance = 4200.0F;
    directional.transition_fraction = 0.12F;
    directional.distance_fadeout_fraction = 0.18F;
    directional.intensity_lux = 95000.0F;

    auto bytes = std::vector<std::byte> {};
    bytes.resize(sizeof(desc) + nodes_bytes + strings.size()
      + sizeof(table_desc) + sizeof(directional));

    auto* cursor = bytes.data();
    std::memcpy(cursor, &desc, sizeof(desc));
    cursor += sizeof(desc);
    std::memcpy(cursor, &root, sizeof(root));
    cursor += sizeof(root);
    std::memcpy(cursor, &sun_node, sizeof(sun_node));
    cursor += sizeof(sun_node);
    std::memcpy(cursor, strings.data(), strings.size());
    cursor += strings.size();
    std::memcpy(cursor, &table_desc, sizeof(table_desc));
    cursor += sizeof(table_desc);
    std::memcpy(cursor, &directional, sizeof(directional));
    return bytes;
  }

  auto BuildSceneDescriptorBytesWithRenderableMaterialOverride(
    const data::AssetKey& geometry_key, const data::AssetKey& material_key)
    -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes.offset = static_cast<data::pak::core::OffsetT>(sizeof(desc));
    desc.nodes.count = 2U;
    desc.nodes.entry_size = sizeof(pakw::NodeRecord);

    auto root = pakw::NodeRecord {};
    root.parent_index = 0U;
    root.scene_name_offset = 1U;

    auto mesh_node = pakw::NodeRecord {};
    mesh_node.parent_index = 0U;
    mesh_node.scene_name_offset = 6U;

    const auto strings = std::string("\0Root\0MeshNode\0", 15);
    const auto nodes_bytes = sizeof(root) + sizeof(mesh_node);
    desc.scene_strings.offset = static_cast<data::pak::core::StringTableOffsetT>(
      desc.nodes.offset + nodes_bytes);
    desc.scene_strings.size
      = static_cast<data::pak::core::StringTableSizeT>(strings.size());
    desc.component_table_directory_offset
      = desc.scene_strings.offset + desc.scene_strings.size;
    desc.component_table_count = 1U;

    auto table_desc = pakw::SceneComponentTableDesc {};
    table_desc.component_type
      = static_cast<uint32_t>(data::ComponentType::kRenderable);
    table_desc.table.offset = desc.component_table_directory_offset
      + static_cast<data::pak::core::OffsetT>(sizeof(table_desc));
    table_desc.table.count = 1U;
    table_desc.table.entry_size = sizeof(pakw::RenderableRecord);

    auto renderable = pakw::RenderableRecord {};
    renderable.node_index = 1U;
    renderable.geometry_key = geometry_key;
    renderable.material_key = material_key;
    renderable.visible = 1U;

    auto bytes = std::vector<std::byte> {};
    bytes.resize(sizeof(desc) + nodes_bytes + strings.size()
      + sizeof(table_desc) + sizeof(renderable));

    auto* cursor = bytes.data();
    std::memcpy(cursor, &desc, sizeof(desc));
    cursor += sizeof(desc);
    std::memcpy(cursor, &root, sizeof(root));
    cursor += sizeof(root);
    std::memcpy(cursor, &mesh_node, sizeof(mesh_node));
    cursor += sizeof(mesh_node);
    std::memcpy(cursor, strings.data(), strings.size());
    cursor += strings.size();
    std::memcpy(cursor, &table_desc, sizeof(table_desc));
    cursor += sizeof(table_desc);
    std::memcpy(cursor, &renderable, sizeof(renderable));
    return bytes;
  }

  auto BuildSingleSubmeshGeometry(std::shared_ptr<const data::MaterialAsset> mat)
    -> std::shared_ptr<data::GeometryAsset>
  {
    using data::GeometryAsset;
    using data::MeshBuilder;
    using data::Vertex;
    using data::pak::geometry::GeometryAssetDesc;

    std::vector<Vertex> vertices {
      { .position = { -1.0F, -1.0F, 0.0F } },
      { .position = { 1.0F, -1.0F, 0.0F } },
      { .position = { 0.0F, 1.0F, 0.0F } },
    };
    std::vector<std::uint32_t> indices { 0U, 1U, 2U };

    auto builder = MeshBuilder {};
    builder.WithVertices(vertices)
      .WithIndices(indices)
      .BeginSubMesh("surface", std::move(mat))
      .WithMeshView({ .first_index = 0U,
        .index_count = static_cast<uint32_t>(indices.size()),
        .first_vertex = 0U,
        .vertex_count = static_cast<uint32_t>(vertices.size()) })
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
      data::AssetKey {}, std::move(desc), std::move(lods));
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

  auto BuildMinimalPhysicsSidecarDescriptorBytes(
    const data::AssetKey& scene_key, const uint32_t node_count,
    const base::Sha256Digest& scene_hash) -> std::vector<std::byte>
  {
    auto desc = pakp::PhysicsSceneAssetDesc {};
    desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
    desc.header.version = pakp::kPhysicsSceneAssetVersion;
    desc.target_scene_key = scene_key;
    desc.target_node_count = node_count;
    std::copy(scene_hash.begin(), scene_hash.end(),
      std::begin(desc.target_scene_content_hash));
    desc.component_table_count = 0;
    desc.component_table_directory_offset = 0;

    auto bytes = std::vector<std::byte>(sizeof(desc));
    std::memcpy(bytes.data(), &desc, sizeof(desc));
    return bytes;
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

    auto PutGeometry(
      const data::AssetKey& key, std::shared_ptr<data::GeometryAsset> geometry)
      -> void
    {
      geometries_.insert_or_assign(key, std::move(geometry));
    }

    auto PutMaterial(
      const data::AssetKey& key, std::shared_ptr<data::MaterialAsset> material)
      -> void
    {
      materials_.insert_or_assign(key, std::move(material));
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
    auto ClearMounts() -> void override { }
    auto ReloadScript(const std::filesystem::path& /*path*/) -> void override {
    }
    auto ReloadAllScripts() -> void override { }
    auto SubscribeScriptReload(ScriptReloadCallback /*callback*/)
      -> EvictionSubscription override
    {
      return {};
    }
    auto TrimCache() -> void override { }

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
    [[nodiscard]] auto EnumerateMountedInputContexts() const
      -> std::vector<MountedInputContextEntry> override
    {
      return {};
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

    [[nodiscard]] auto GetTexture(content::ResourceKey /*key*/) const noexcept
      -> std::shared_ptr<data::TextureResource> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetBuffer(content::ResourceKey /*key*/) const noexcept
      -> std::shared_ptr<data::BufferResource> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetMaterialAsset(
      const data::AssetKey& key) const noexcept
      -> std::shared_ptr<data::MaterialAsset> override
    {
      const auto it = materials_.find(key);
      return it == materials_.end() ? nullptr : it->second;
    }
    [[nodiscard]] auto GetGeometryAsset(
      const data::AssetKey& key) const noexcept
      -> std::shared_ptr<data::GeometryAsset> override
    {
      const auto it = geometries_.find(key);
      return it == geometries_.end() ? nullptr : it->second;
    }
    [[nodiscard]] auto GetScriptAsset(
      const data::AssetKey& /*key*/) const noexcept
      -> std::shared_ptr<data::ScriptAsset> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetScriptResource(
      content::ResourceKey /*key*/) const noexcept
      -> std::shared_ptr<data::ScriptResource> override
    {
      return nullptr;
    }
    auto LoadScriptResourceAsync(content::ResourceKey /*key*/)
      -> co::Co<std::shared_ptr<data::ScriptResource>> override
    {
      co_return nullptr;
    }
    [[nodiscard]] auto MakeScriptResourceKeyForAsset(
      const data::AssetKey& /*context_asset_key*/,
      data::pak::core::ResourceIndexT /*resource_index*/) const noexcept
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto ReadScriptResourceForAsset(
      const data::AssetKey& /*context_asset_key*/,
      data::pak::core::ResourceIndexT /*resource_index*/) const
      -> std::shared_ptr<const data::ScriptResource> override
    {
      return nullptr;
    }

    [[nodiscard]] auto GetPhysicsSceneAsset(
      const data::AssetKey& key) const noexcept
      -> std::shared_ptr<data::PhysicsSceneAsset> override
    {
      const auto it = sidecars_.find(key);
      return it == sidecars_.end() ? nullptr : it->second;
    }
    [[nodiscard]] auto GetPhysicsResource(
      content::ResourceKey /*key*/) const noexcept
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
      data::pak::core::ResourceIndexT /*resource_index*/) const noexcept
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
      const data::AssetKey& /*context_asset_key*/,
      data::pak::core::ResourceIndexT /*resource_index*/) const noexcept
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
      const data::AssetKey& /*context_asset_key*/,
      const data::AssetKey& /*resource_asset_key*/) const noexcept
      -> std::optional<content::ResourceKey> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto ReadCollisionShapeAssetDescForAsset(
      const data::AssetKey& /*context_asset_key*/,
      const data::AssetKey& /*shape_asset_key*/) const
      -> std::optional<data::pak::physics::CollisionShapeAssetDesc> override
    {
      return std::nullopt;
    }
    [[nodiscard]] auto ReadPhysicsMaterialAssetDescForAsset(
      const data::AssetKey& /*context_asset_key*/,
      const data::AssetKey& /*material_asset_key*/) const
      -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc> override
    {
      return std::nullopt;
    }

    [[nodiscard]] auto GetInputActionAsset(
      const data::AssetKey& /*key*/) const noexcept
      -> std::shared_ptr<data::InputActionAsset> override
    {
      return nullptr;
    }
    [[nodiscard]] auto GetInputMappingContextAsset(
      const data::AssetKey& /*key*/) const noexcept
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
      const data::AssetKey& scene_key) const
      -> std::optional<data::AssetKey> override
    {
      const auto it = sidecar_keys_by_scene_.find(scene_key);
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

    auto ReleaseResource(content::ResourceKey /*key*/) -> bool override
    {
      return false;
    }
    auto PinResource(content::ResourceKey /*key*/) -> bool override
    {
      return false;
    }
    auto UnpinResource(content::ResourceKey /*key*/) -> bool override
    {
      return false;
    }

    auto ReleaseAsset(const data::AssetKey& /*key*/) -> bool override
    {
      return false;
    }
    auto PinAsset(const data::AssetKey& /*key*/) -> bool override
    {
      return false;
    }
    auto UnpinAsset(const data::AssetKey& /*key*/) -> bool override
    {
      return false;
    }

    auto SubscribeResourceEvictions(TypeId resource_type,
      EvictionHandler handler) -> EvictionSubscription override
    {
      const auto id = next_subscription_id_++;
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

  private:
    void UnsubscribeResourceEvictions(
      TypeId resource_type, const uint64_t id) noexcept override
    {
      const auto it = eviction_handlers_.find(resource_type);
      if (it == eviction_handlers_.end()) {
        return;
      }
      it->second.erase(id);
    }

    content::ResidencyPolicy residency_policy_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::SceneAsset>>
      scenes_ {};
    std::unordered_map<data::AssetKey, data::AssetKey>
      sidecar_keys_by_scene_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::PhysicsSceneAsset>>
      sidecars_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::GeometryAsset>>
      geometries_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::MaterialAsset>>
      materials_ {};
    std::unordered_map<TypeId, std::unordered_map<uint64_t, EvictionHandler>>
      eviction_handlers_ {};
    uint64_t next_subscription_id_ { 1U };
    std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };
    uint64_t next_resource_key_ { 1U };
  };
} // namespace

NOLINT_TEST(SceneLoaderServicePhase4Test,
  StartLoadFailsWhenTargetSceneContentHashMismatches)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/identity.oscene");
  const auto sidecar_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/identity.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());
  auto mismatched_hash = scene_hash;
  mismatched_hash[0] ^= 0xFFU;

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 1U, mismatched_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(
  SceneLoaderServicePhase4Test, StartLoadFailsWhenTargetSceneKeyMismatches)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/key_mismatch.oscene");
  const auto foreign_scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/foreign.oscene");
  const auto sidecar_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/key_mismatch.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(
      foreign_scene_key, 1U, scene_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(
  SceneLoaderServicePhase4Test, StartLoadFailsWhenTargetNodeCountMismatches)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/node_count_mismatch.oscene");
  const auto sidecar_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/node_count_mismatch.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 2U, scene_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  StartLoadSucceedsWhenSceneIdentityHashKeyAndNodeCountMatch)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/valid.oscene");
  const auto sidecar_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/valid.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 1U, scene_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  ASSERT_TRUE(service->IsReady());
  EXPECT_FALSE(service->IsFailed());
  auto result = service->GetResult();
  EXPECT_EQ(result.scene_key, scene_key);
  EXPECT_THAT(result.asset, ::testing::NotNull());
  EXPECT_THAT(result.physics_asset, ::testing::NotNull());
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  BuildSceneAsyncHydratesDirectionalLightCsmSettings)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/sun_csm.oscene");
  auto scene_asset = std::make_shared<data::SceneAsset>(
    scene_key, BuildSceneDescriptorBytesWithDirectionalLight());
  loader.PutScene(scene_key, scene_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  auto runtime_scene
    = std::make_shared<scene::Scene>("DemoShell.DirectionalCsmHydration", 64U);
  oxygen::co::testing::TestEventLoop loop;
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    co_await service->BuildSceneAsync(*runtime_scene, *scene_asset);
  });

  auto sun_node = FindNodeByName(*runtime_scene, "SunNode");
  ASSERT_TRUE(sun_node.has_value());
  auto light = sun_node->GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());

  EXPECT_TRUE(light->get().Common().casts_shadows);
  EXPECT_FLOAT_EQ(light->get().Common().shadow.bias, 0.0007F);
  EXPECT_FLOAT_EQ(light->get().Common().shadow.normal_bias, 0.03F);
  EXPECT_TRUE(light->get().GetEnvironmentContribution());
  EXPECT_TRUE(light->get().IsSunLight());
  EXPECT_FLOAT_EQ(light->get().GetIntensityLux(), 95000.0F);

  const auto& csm = light->get().CascadedShadows();
  EXPECT_EQ(csm.cascade_count, 4U);
  EXPECT_EQ(csm.split_mode, scene::DirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(csm.max_shadow_distance, 4200.0F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[0], 250.0F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[1], 900.0F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[2], 2200.0F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[3], 4200.0F);
  EXPECT_FLOAT_EQ(csm.distribution_exponent, 2.5F);
  EXPECT_FLOAT_EQ(csm.transition_fraction, 0.12F);
  EXPECT_FLOAT_EQ(csm.distance_fadeout_fraction, 0.18F);
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  BuildSceneAsyncHydratesLocalFogVolumeComponents)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/local_fog.oscene");
  auto scene_asset = std::make_shared<data::SceneAsset>(
    scene_key, BuildSceneDescriptorBytesWithLocalFog());
  loader.PutScene(scene_key, scene_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  auto runtime_scene
    = std::make_shared<scene::Scene>("DemoShell.LocalFogHydration", 64U);
  oxygen::co::testing::TestEventLoop loop;
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    co_await service->BuildSceneAsync(*runtime_scene, *scene_asset);
  });

  auto fog_node = FindNodeByName(*runtime_scene, "FogNode");
  ASSERT_TRUE(fog_node.has_value());
  const auto impl_opt = fog_node->GetImpl();
  ASSERT_TRUE(impl_opt.has_value());
  ASSERT_TRUE(
    impl_opt->get().HasComponent<scene::environment::LocalFogVolume>());

  const auto& local_fog
    = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
  EXPECT_TRUE(local_fog.IsEnabled());
  EXPECT_FLOAT_EQ(local_fog.GetRadialFogExtinction(), 0.3F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogExtinction(), 0.2F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogFalloff(), 0.15F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogOffset(), 1.25F);
  EXPECT_FLOAT_EQ(local_fog.GetFogPhaseG(), 0.4F);
  EXPECT_EQ(local_fog.GetFogAlbedo(), glm::vec3(0.7F, 0.8F, 0.9F));
  EXPECT_EQ(local_fog.GetFogEmissive(), glm::vec3(0.1F, 0.2F, 0.3F));
  EXPECT_EQ(local_fog.GetSortPriority(), 2);
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  BuildSceneAsyncAppliesRenderableMaterialOverride)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/renderable_override.oscene");
  const auto geometry_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/cube.ogeo");
  const auto material_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/override.omat");

  auto base_material = data::MaterialAsset::CreateDefault();
  auto override_material = BuildTestMaterial(material_key);
  auto geometry = BuildSingleSubmeshGeometry(base_material);

  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key,
    BuildSceneDescriptorBytesWithRenderableMaterialOverride(
      geometry_key, material_key));
  loader.PutScene(scene_key, scene_asset);
  loader.PutGeometry(geometry_key, geometry);
  loader.PutMaterial(material_key, override_material);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  auto runtime_scene
    = std::make_shared<scene::Scene>("DemoShell.RenderableOverride", 64U);
  oxygen::co::testing::TestEventLoop loop;
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    co_await service->BuildSceneAsync(*runtime_scene, *scene_asset);
  });

  auto mesh_node = FindNodeByName(*runtime_scene, "MeshNode");
  ASSERT_TRUE(mesh_node.has_value());
  auto renderable = mesh_node->GetRenderable();
  ASSERT_TRUE(renderable.HasGeometry());
  EXPECT_EQ(renderable.ResolveSubmeshMaterial(0U, 0U), override_material);
}

} // namespace oxygen::examples::testing
