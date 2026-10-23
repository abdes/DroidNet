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
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DemoShell/Services/SceneLoaderService.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResidencyPin.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
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
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>
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
  namespace pakp = data::pak::physics;

  // Scene string tables used by the descriptor builders start with the empty
  // name; each node name offset points just past the previous name.
  constexpr uint32_t kRootNameOffset = 1U;
  constexpr uint32_t kSecondNameOffset = 6U; // after "\0Root\0"
  constexpr uint32_t kThirdNameOffset = 16U; // after "\0Root\0Inherited\0"

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

  template <typename T>
  auto AppendRecord(std::vector<std::byte>& bytes, const T& record) -> void
  {
    const auto view = std::as_bytes(std::span(&record, 1));
    bytes.insert(bytes.end(), view.begin(), view.end());
  }

  auto AppendText(std::vector<std::byte>& bytes, std::string_view text) -> void
  {
    const auto view = std::as_bytes(std::span(text));
    bytes.insert(bytes.end(), view.begin(), view.end());
  }

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
    desc.header.strings.offset
      = static_cast<data::pak::core::StringTableOffsetT>(
        desc.nodes.offset + static_cast<data::pak::core::OffsetT>(nodes_bytes));
    desc.header.strings.size = 1;

    auto bytes = std::vector<std::byte> {};
    AppendRecord(bytes, desc);
    for (uint32_t i = 0; i < node_count; ++i) {
      AppendRecord(bytes, pakw::NodeRecord {});
    }
    AppendText(bytes, std::string_view("\0", 1));
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
    root.scene_name_offset = kRootNameOffset;

    auto fog_node = pakw::NodeRecord {};
    fog_node.parent_index = 0U;
    fog_node.scene_name_offset = kSecondNameOffset;

    constexpr auto strings = std::string_view("\0Root\0FogNode\0", 14);
    const auto nodes_bytes = sizeof(root) + sizeof(fog_node);
    desc.header.strings.offset
      = static_cast<data::pak::core::StringTableOffsetT>(
        desc.nodes.offset + nodes_bytes);
    desc.header.strings.size
      = static_cast<data::pak::core::StringTableSizeT>(strings.size());
    desc.component_table_directory_offset
      = desc.header.strings.offset + desc.header.strings.size;
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
    local_fog.radial_fog_extinction = kFogRadialExtinction;
    local_fog.height_fog_extinction = kFogHeightExtinction;
    local_fog.height_fog_falloff = kFogHeightFalloff;
    local_fog.height_fog_offset = kFogHeightOffset;
    local_fog.fog_phase_g = kFogPhaseG;
    std::ranges::copy(kFogAlbedo, std::begin(local_fog.fog_albedo));
    std::ranges::copy(kFogEmissive, std::begin(local_fog.fog_emissive));
    local_fog.sort_priority = kFogSortPriority;

    auto bytes = std::vector<std::byte> {};
    AppendRecord(bytes, desc);
    AppendRecord(bytes, root);
    AppendRecord(bytes, fog_node);
    AppendText(bytes, strings);
    AppendRecord(bytes, table_desc);
    AppendRecord(bytes, local_fog);
    return bytes;
  }

  auto BuildSceneDescriptorBytesWithFlagModes() -> std::vector<std::byte>
  {
    auto descriptor = pakw::SceneAssetDesc {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kScene);
    descriptor.header.version = pakw::kSceneAssetVersion;
    descriptor.nodes.offset = sizeof(descriptor);
    descriptor.nodes.count = 3U;
    descriptor.nodes.entry_size = sizeof(pakw::NodeRecord);
    auto nodes = std::array<pakw::NodeRecord, 3> {};
    nodes.at(0).scene_name_offset = kRootNameOffset;
    nodes.at(1).scene_name_offset = kSecondNameOffset;
    nodes.at(1).inherited_flags = pakw::kSceneNodeFlags_Inheritable;
    nodes.at(2).scene_name_offset = kThirdNameOffset;
    nodes.at(2).node_flags = pakw::kSceneNodeFlag_Visible;
    nodes.at(2).inherited_flags = pakw::kSceneNodeFlag_ReceivesShadows;
    constexpr auto strings = std::string_view("\0Root\0Inherited\0Local\0", 22);
    descriptor.header.strings.offset = sizeof(descriptor) + sizeof(nodes);
    descriptor.header.strings.size = static_cast<uint32_t>(strings.size());
    auto bytes = std::vector<std::byte> {};
    AppendRecord(bytes, descriptor);
    AppendRecord(bytes, nodes);
    AppendText(bytes, strings);
    return bytes;
  }

  auto BuildSceneDescriptorBytesWithDirectionalLight() -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes.offset = static_cast<data::pak::core::OffsetT>(sizeof(desc));
    desc.nodes.count = 2U;
    desc.nodes.entry_size = sizeof(pakw::NodeRecord);

    auto root = pakw::NodeRecord {};
    root.parent_index = 0U;
    root.scene_name_offset = kRootNameOffset;

    auto sun_node = pakw::NodeRecord {};
    sun_node.parent_index = 0U;
    sun_node.scene_name_offset = kSecondNameOffset;

    constexpr auto strings = std::string_view("\0Root\0SunNode\0", 14);
    const auto nodes_bytes = sizeof(root) + sizeof(sun_node);
    desc.header.strings.offset
      = static_cast<data::pak::core::StringTableOffsetT>(
        desc.nodes.offset + nodes_bytes);
    desc.header.strings.size
      = static_cast<data::pak::core::StringTableSizeT>(strings.size());
    desc.component_table_directory_offset
      = desc.header.strings.offset + desc.header.strings.size;
    desc.component_table_count = 1U;

    auto table_desc = pakw::SceneComponentTableDesc {};
    table_desc.component_type
      = static_cast<uint32_t>(data::ComponentType::kDirectionalLight);
    table_desc.table.offset = desc.component_table_directory_offset
      + static_cast<data::pak::core::OffsetT>(sizeof(table_desc));
    table_desc.table.count = 1U;
    table_desc.table.entry_size = sizeof(pakw::DirectionalLightRecord);

    auto directional = pakw::MakeDefaultRecord<pakw::DirectionalLightRecord>();
    directional.node_index = 1U;
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

    auto bytes = std::vector<std::byte> {};
    AppendRecord(bytes, desc);
    AppendRecord(bytes, root);
    AppendRecord(bytes, sun_node);
    AppendText(bytes, strings);
    AppendRecord(bytes, table_desc);
    AppendRecord(bytes, directional);
    return bytes;
  }

  //! Assets a renderable material override scene refers to.
  struct RenderableOverrideKeys final {
    data::AssetKey geometry;
    data::AssetKey material;
  };

  auto BuildSceneDescriptorBytesWithRenderableMaterialOverride(
    const RenderableOverrideKeys& keys, const data::GeometryAsset& geometry)
    -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes = {
      .offset = sizeof(desc),
      .count = 2,
      .entry_size = sizeof(pakw::NodeRecord),
    };
    auto root = pakw::NodeRecord {};
    root.scene_name_offset = kRootNameOffset;
    auto mesh_node = pakw::NodeRecord {};
    mesh_node.parent_index = 0;
    mesh_node.scene_name_offset = kSecondNameOffset;
    constexpr auto strings = std::string_view("\0Root\0MeshNode\0", 15);
    desc.header.strings = {
      .offset = static_cast<data::pak::core::StringTableOffsetT>(
        desc.nodes.offset + sizeof(root) + sizeof(mesh_node)),
      .size = static_cast<data::pak::core::StringTableSizeT>(strings.size()),
    };
    desc.component_table_directory_offset
      = desc.header.strings.offset + desc.header.strings.size;
    desc.component_table_count = 2;
    const auto record_offset = desc.component_table_directory_offset
      + (2 * sizeof(pakw::SceneComponentTableDesc));
    const auto tables = std::array {
      pakw::SceneComponentTableDesc {
        .component_type
        = static_cast<uint32_t>(data::ComponentType::kRenderable),
        .table = { .offset = record_offset,
          .count = 1,
          .entry_size = sizeof(pakw::RenderableRecord), },
      },
      pakw::SceneComponentTableDesc {
        .component_type
        = static_cast<uint32_t>(data::ComponentType::kOverride),
        .table = { .offset = record_offset + sizeof(pakw::RenderableRecord),
          .count = 1,
          .entry_size = sizeof(pakw::OverrideRecord), },
      },
    };
    const auto renderable = pakw::RenderableRecord {
      .node_index = 1,
      .geometry_key = keys.geometry,
    };
    const auto& inventory = geometry.Surfaces();
    auto assignment = pakw::OverrideRecord {
      .node_index = 1,
      .override_id = data::schema::OverrideId::kGeometrySurfaceMaterial,
      .part_id = inventory.surfaces.at(0).surface_id,
      .layout_revision = inventory.layout_revision,
    };
    assignment.value.as_asset = keys.material;
    auto environment = pakw::SceneEnvironmentBlockHeader {};
    environment.byte_size = sizeof(environment);
    auto bytes = std::vector<std::byte> {};
    AppendRecord(bytes, desc);
    AppendRecord(bytes, root);
    AppendRecord(bytes, mesh_node);
    AppendText(bytes, strings);
    for (const auto& table : tables) {
      AppendRecord(bytes, table);
    }
    AppendRecord(bytes, renderable);
    AppendRecord(bytes, assignment);
    AppendRecord(bytes, environment);
    return bytes;
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
    std::ranges::copy(scene_hash, std::begin(desc.target_scene_content_hash));
    desc.component_table_count = 0;
    desc.component_table_directory_offset = 0;

    return data::MakeDescriptorBytes(desc);
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
      last_physics_source_ = source;
      const auto asset = GetPhysicsSceneAsset(key);
      on_complete(asset && asset->GetSourceKey() == source ? asset : nullptr);
    }

    [[nodiscard]] auto LastPhysicsSource() const -> data::SourceKey
    {
      return last_physics_source_;
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
    [[nodiscard]] auto ReadScriptResourceForAsset(
      const data::Asset& /*context_asset*/,
      data::ResourceReferenceIndex /*resource_index*/) const
      -> std::shared_ptr<const data::ScriptResource> override
    {
      return nullptr;
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
    [[nodiscard]] auto ReadCollisionShapeAssetDescForAsset(
      const data::Asset& /*context_asset*/,
      const data::AssetKey& /*shape_asset_key*/) const
      -> std::optional<data::pak::physics::CollisionShapeAssetDesc> override
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
      const data::Asset& scene, const content::ContentLoadScope& /*scope*/)
      -> std::optional<data::AssetKey> override
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
    data::SourceKey last_physics_source_ {};
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
  mismatched_hash.at(0) = static_cast<std::uint8_t>(~mismatched_hash.at(0));

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 1U, mismatched_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto service = MakeService(loader);

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

  auto service = MakeService(loader);

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

  auto service = MakeService(loader);

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  StartLoadSucceedsWhenSceneIdentityHashKeyAndNodeCountMatch)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto parsed_source
    = data::SourceKey::FromString("019ba912-6b80-7000-8000-000000000001");
  ASSERT_TRUE(parsed_source.has_value());
  const auto source = parsed_source.value();
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/valid.oscene");
  const auto sidecar_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/valid.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(
    scene_key, scene_bytes, data::SourceOrigin { .key = source });
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 1U, scene_hash),
    data::SourceOrigin { .key = source });

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto service = MakeService(loader);

  service->StartLoad(scene_key);

  ASSERT_TRUE(service->IsReady());
  EXPECT_FALSE(service->IsFailed());
  auto result = service->GetResult();
  EXPECT_EQ(result.scene_key, scene_key);
  EXPECT_THAT(result.asset, ::testing::NotNull());
  EXPECT_THAT(result.physics_asset, ::testing::NotNull());
  EXPECT_TRUE(loader.LastPhysicsSource().IsNil());
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  BuildSceneAsyncHydratesIndependentNodeFlagSourceModes)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto key = data::AssetKey::FromVirtualPath("/Game/Tests/flags.oscene");
  const auto asset = std::make_shared<data::SceneAsset>(
    key, BuildSceneDescriptorBytesWithFlagModes());
  loader.PutScene(key, asset);
  auto service = MakeService(loader);
  auto runtime_scene
    = std::make_shared<scene::Scene>("FlagsHydration", kRuntimeSceneCapacity);
  BuildScene(*service, *runtime_scene, asset);
  runtime_scene->Update();
  auto root = FindNodeByName(*runtime_scene, "Root");
  auto inherited = FindNodeByName(*runtime_scene, "Inherited");
  auto local = FindNodeByName(*runtime_scene, "Local");
  if (!root.has_value()) {
    FAIL() << "Expected root to contain a value";
  }
  if (!inherited.has_value()) {
    FAIL() << "Expected inherited to contain a value";
  }
  if (!local.has_value()) {
    FAIL() << "Expected local to contain a value";
  }
  const auto root_flags = root->GetFlags();
  const auto inherited_flags = inherited->GetFlags();
  const auto local_flags = local->GetFlags();
  if (!root_flags.has_value()) {
    FAIL() << "Expected root_flags to contain a value";
  }
  if (!inherited_flags.has_value()) {
    FAIL() << "Expected inherited_flags to contain a value";
  }
  if (!local_flags.has_value()) {
    FAIL() << "Expected local_flags to contain a value";
  }
  using scene::SceneNodeFlags;
  for (const auto flag : {
         SceneNodeFlags::kVisible,
         SceneNodeFlags::kCastsShadows,
         SceneNodeFlags::kReceivesShadows,
       }) {
    EXPECT_TRUE(inherited_flags->get().IsInherited(flag));
    EXPECT_FALSE(inherited_flags->get().GetEffectiveValue(flag));
    root_flags->get().SetLocalValue(flag, true);
  }
  EXPECT_FALSE(local_flags->get().IsInherited(SceneNodeFlags::kVisible));
  EXPECT_TRUE(local_flags->get().GetEffectiveValue(SceneNodeFlags::kVisible));
  EXPECT_FALSE(local_flags->get().IsInherited(SceneNodeFlags::kCastsShadows));
  EXPECT_FALSE(
    local_flags->get().GetEffectiveValue(SceneNodeFlags::kCastsShadows));
  EXPECT_TRUE(local_flags->get().IsInherited(SceneNodeFlags::kReceivesShadows));
  runtime_scene->Update();
  for (const auto flag : {
         SceneNodeFlags::kVisible,
         SceneNodeFlags::kCastsShadows,
         SceneNodeFlags::kReceivesShadows,
       }) {
    EXPECT_TRUE(inherited_flags->get().GetEffectiveValue(flag));
  }
  EXPECT_FALSE(
    local_flags->get().GetEffectiveValue(SceneNodeFlags::kCastsShadows));
  EXPECT_TRUE(
    local_flags->get().GetEffectiveValue(SceneNodeFlags::kReceivesShadows));
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

  auto service = MakeService(loader);

  auto runtime_scene = std::make_shared<scene::Scene>(
    "DemoShell.DirectionalCsmHydration", kRuntimeSceneCapacity);
  BuildScene(*service, *runtime_scene, scene_asset);

  auto sun_node = FindNodeByName(*runtime_scene, "SunNode");
  if (!sun_node.has_value()) {
    FAIL() << "Expected sun_node to contain a value";
  }
  auto light = sun_node->GetLightAs<scene::DirectionalLight>();
  if (!light.has_value()) {
    FAIL() << "Expected light to contain a value";
  }

  EXPECT_TRUE(light->get().Common().casts_shadows);
  EXPECT_FLOAT_EQ(light->get().Common().shadow.bias, kSunShadowBias);
  EXPECT_FLOAT_EQ(
    light->get().Common().shadow.normal_bias, kSunShadowNormalBias);

  EXPECT_TRUE(light->get().GetAtmosphereLightSlot()
    == scene::AtmosphereLightSlot::kPrimary);
  EXPECT_FLOAT_EQ(light->get().GetIntensityLux(), kSunIntensityLux);

  const auto& csm = light->get().CascadedShadows();
  EXPECT_EQ(csm.cascade_count, kSunCascadeDistances.size());
  EXPECT_EQ(csm.split_mode, scene::DirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(csm.max_shadow_distance, kSunMaxShadowDistance);
  for (size_t i = 0; i < kSunCascadeDistances.size(); ++i) {
    EXPECT_FLOAT_EQ(csm.cascade_distances.at(i), kSunCascadeDistances.at(i));
  }
  EXPECT_FLOAT_EQ(csm.distribution_exponent, kSunDistributionExponent);
  EXPECT_FLOAT_EQ(csm.transition_fraction, kSunTransitionFraction);
  EXPECT_FLOAT_EQ(csm.distance_fadeout_fraction, kSunFadeoutFraction);
}

NOLINT_TEST(
  SceneLoaderServicePhase4Test, BuildSceneAsyncHydratesLocalFogVolumeComponents)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/local_fog.oscene");
  auto scene_asset = std::make_shared<data::SceneAsset>(
    scene_key, BuildSceneDescriptorBytesWithLocalFog());
  loader.PutScene(scene_key, scene_asset);

  auto service = MakeService(loader);

  auto runtime_scene = std::make_shared<scene::Scene>(
    "DemoShell.LocalFogHydration", kRuntimeSceneCapacity);
  BuildScene(*service, *runtime_scene, scene_asset);

  auto fog_node = FindNodeByName(*runtime_scene, "FogNode");
  if (!fog_node.has_value()) {
    FAIL() << "Expected fog_node to contain a value";
  }
  const auto impl_opt = fog_node->GetImpl();
  if (!impl_opt.has_value()) {
    FAIL() << "Expected impl_opt to contain a value";
  }
  ASSERT_TRUE(
    impl_opt->get().HasComponent<scene::environment::LocalFogVolume>());

  const auto& local_fog
    = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
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
      { .geometry = geometry_key, .material = material_key }, *geometry));
  loader.PutScene(scene_key, scene_asset);
  loader.PutGeometry(geometry_key, geometry);
  loader.PutMaterial(material_key, override_material);

  auto service = MakeService(loader);

  auto runtime_scene = std::make_shared<scene::Scene>(
    "DemoShell.RenderableOverride", kRuntimeSceneCapacity);
  BuildScene(*service, *runtime_scene, scene_asset);

  auto mesh_node = FindNodeByName(*runtime_scene, "MeshNode");
  if (!mesh_node.has_value()) {
    FAIL() << "Expected mesh_node to contain a value";
  }
  auto renderable = mesh_node->GetRenderable();
  ASSERT_TRUE(renderable.HasGeometry());
  EXPECT_EQ(renderable.ResolveSubmeshMaterial(
              oxygen::data::LodIndex { 0U }, oxygen::data::SubmeshIndex { 0U }),
    override_material);
}

} // namespace oxygen::examples::testing
