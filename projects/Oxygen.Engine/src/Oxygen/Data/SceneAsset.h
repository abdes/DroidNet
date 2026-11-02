//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//=== Component Traits & Type List ===---------------------------------------//

//! Traits class to map component record types to their ComponentType enum.
template <typename T> struct ComponentTraits;

template <> struct ComponentTraits<pak::world::RenderableRecord> {
  static constexpr ComponentType kType = ComponentType::kRenderable;
};

template <> struct ComponentTraits<pak::world::OverrideRecord> {
  static constexpr ComponentType kType = ComponentType::kOverride;
};

//! The LOD policy values of the overrides, by `OverrideRecordIndex`.
template <> struct ComponentTraits<pak::geometry::LodPolicyRecord> {
  static constexpr ComponentType kType = ComponentType::kOverrideLodPolicy;
};

template <> struct ComponentTraits<pak::world::LocalFogVolumeRecord> {
  static constexpr ComponentType kType = ComponentType::kLocalFogVolume;
};

template <> struct ComponentTraits<pak::world::PerspectiveCameraRecord> {
  static constexpr ComponentType kType = ComponentType::kPerspectiveCamera;
};

template <> struct ComponentTraits<pak::world::OrthographicCameraRecord> {
  static constexpr ComponentType kType = ComponentType::kOrthographicCamera;
};

template <> struct ComponentTraits<pak::world::DirectionalLightRecord> {
  static constexpr ComponentType kType = ComponentType::kDirectionalLight;
};

template <> struct ComponentTraits<pak::world::PointLightRecord> {
  static constexpr ComponentType kType = ComponentType::kPointLight;
};

template <> struct ComponentTraits<pak::world::SpotLightRecord> {
  static constexpr ComponentType kType = ComponentType::kSpotLight;
};

template <> struct ComponentTraits<pak::scripting::ScriptingComponentRecord> {
  static constexpr ComponentType kType = ComponentType::kScripting;
};

template <> struct ComponentTraits<pak::world::SceneInstanceRecord> {
  static constexpr ComponentType kType = ComponentType::kSceneInstance;
};

template <> struct ComponentTraits<pak::world::StartingInputContextRecord> {
  static constexpr ComponentType kType = ComponentType::kStartingInputContext;
};

template <> struct ComponentTraits<pak::world::NodePathRecord> {
  static constexpr ComponentType kType = ComponentType::kNodePath;
};

template <> struct ComponentTraits<pak::world::NodePathElement> {
  static constexpr ComponentType kType = ComponentType::kNodePathElement;
};

template <> struct ComponentTraits<pak::scripting::ScriptParamOverrideRecord> {
  static constexpr ComponentType kType = ComponentType::kScriptParamOverride;
};

//! The record types of the component tables a scene holds, one table per
//! type; the reader refuses a table of any other type.
using SceneComponentRecords = std::tuple<pak::world::RenderableRecord,
  pak::world::OverrideRecord, pak::geometry::LodPolicyRecord,
  pak::world::LocalFogVolumeRecord, pak::world::PerspectiveCameraRecord,
  pak::world::OrthographicCameraRecord, pak::world::DirectionalLightRecord,
  pak::world::PointLightRecord, pak::world::SpotLightRecord,
  pak::scripting::ScriptingComponentRecord, pak::world::SceneInstanceRecord,
  pak::world::StartingInputContextRecord, pak::world::NodePathRecord,
  pak::world::NodePathElement, pak::scripting::ScriptParamOverrideRecord>;

//! A record of a node's component: tables of them hold one record per node,
//! sorted by node.
template <typename T>
concept NodeComponentRecord = requires(const T& record) { record.node_index; };

//! Traits class to map environment records to their system type.
template <typename T> struct EnvironmentTraits;

template <>
struct EnvironmentTraits<pak::world::SkyAtmosphereEnvironmentRecord> {
  static constexpr auto kType
    = pak::world::EnvironmentComponentType::kSkyAtmosphere;
};

template <>
struct EnvironmentTraits<pak::world::VolumetricCloudsEnvironmentRecord> {
  static constexpr auto kType
    = pak::world::EnvironmentComponentType::kVolumetricClouds;
};

template <> struct EnvironmentTraits<pak::world::FogEnvironmentRecord> {
  static constexpr auto kType = pak::world::EnvironmentComponentType::kFog;
};

template <> struct EnvironmentTraits<pak::world::SkyLightEnvironmentRecord> {
  static constexpr auto kType = pak::world::EnvironmentComponentType::kSkyLight;
};

template <> struct EnvironmentTraits<pak::world::SkySphereEnvironmentRecord> {
  static constexpr auto kType
    = pak::world::EnvironmentComponentType::kSkySphere;
};

template <>
struct EnvironmentTraits<pak::world::PostProcessVolumeEnvironmentRecord> {
  static constexpr auto kType
    = pak::world::EnvironmentComponentType::kPostProcessVolume;
};

template <> struct EnvironmentTraits<pak::world::BackgroundEnvironmentRecord> {
  static constexpr auto kType
    = pak::world::EnvironmentComponentType::kBackground;
};

//! The environment records a scene holds, at most one of each type.
using SceneEnvironmentRecords
  = std::tuple<pak::world::SkyAtmosphereEnvironmentRecord,
    pak::world::VolumetricCloudsEnvironmentRecord,
    pak::world::FogEnvironmentRecord, pak::world::SkyLightEnvironmentRecord,
    pak::world::SkySphereEnvironmentRecord,
    pak::world::PostProcessVolumeEnvironmentRecord,
    pak::world::BackgroundEnvironmentRecord>;

namespace detail {

  //! The decoded storage of a list of scene record types: a table of each,
  //! or at most one of each.
  template <typename Records> struct SceneRecordStorage;

  template <typename... Records>
  struct SceneRecordStorage<std::tuple<Records...>> {
    using Tables = std::tuple<std::vector<Records>...>;
    using Optionals = std::tuple<std::optional<Records>...>;
  };

} // namespace detail

//! Represents a loaded Scene asset.
/*!
  SceneAsset is the validating reader of a cooked scene descriptor
  (SceneAssetDesc) and the read-only model of what it holds: the node
  hierarchy, the string table, the component tables and the environment
  records.

  ### Admission
  Construction decodes and checks the whole descriptor and refuses it with a
  `CookedOpenError` naming the rule it breaks. An admitted scene is immutable:
  its accessors only read what construction decoded, so any thread may read
  it.

  ### Storage
  The descriptor bytes must outlive a scene constructed over a span; a scene
  constructed from a vector owns them. Script slots and environment record
  views refer to those bytes.

  ### Usage
  ```cpp
  // Load raw bytes from PAK
  std::vector<uint8_t> raw_data = ...;

  // Create SceneAsset view
  SceneAsset scene(key, raw_data);

  // Iterate nodes
  for (const auto& node : scene.GetNodes()) {
      auto name = scene.GetNodeName(node);
      // ...
  }

  // Access components
  auto renderables = scene.GetComponents<pak::world::RenderableRecord>();
  for (const auto& renderable : renderables) {
      // ...
  }
  ```
*/
class SceneAsset final : public Asset {
  OXYGEN_TYPED(SceneAsset)
public:
  //! View over a single environment-system record stored in the environment
  //! block.
  struct EnvironmentSystemRecordView {
    pak::world::SceneEnvironmentSystemRecordHeader header {};
    //! Byte offset relative to the beginning of this scene descriptor.
    size_t record_offset { 0U };
    std::span<const std::byte> bytes;
  };

  //! Constructs a SceneAsset from a raw data blob.
  /*!
    @param key The stable asset key.
    @param data The scene descriptor, exactly.
    @throw CookedOpenError When the descriptor breaks a rule of its format.
  */
  OXGN_DATA_API SceneAsset(AssetKey key, std::span<const std::byte> data,
    SourceOrigin source_origin = {});

  //! Constructs a SceneAsset that owns its raw data.
  /*!
    This is the preferred construction path for loaders.

    @param key The stable asset key.
    @param data The scene descriptor, exactly.
    @throw CookedOpenError When the descriptor breaks a rule of its format.
  */
  OXGN_DATA_API SceneAsset(
    AssetKey key, std::vector<std::byte> data, SourceOrigin source_origin = {});

  ~SceneAsset() override = default;

  OXYGEN_DEFAULT_COPYABLE(SceneAsset)
  OXYGEN_DEFAULT_MOVABLE(SceneAsset)

  //=== Asset Interface ===---------------------------------------------------//

  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::core::AssetHeader& override
  {
    return desc_.header;
  }

  //! Returns the raw descriptor payload bytes for integrity checks.
  [[nodiscard]] auto GetRawData() const noexcept -> std::span<const std::byte>
  {
    return data_;
  }

  //=== Node Access ===-------------------------------------------------------//

  //! Script slots and parameters belong to this immutable scene generation.
  OXGN_DATA_NDAPI auto ReadScriptSlots(uint32_t start, uint32_t count) const
    -> std::vector<pak::scripting::ScriptSlotRecord>;
  OXGN_DATA_NDAPI auto ReadScriptParameters(
    const pak::scripting::ScriptSlotRecord& slot) const
    -> std::vector<pak::scripting::ScriptParamRecord>;

  //! Returns a view of all nodes in the scene.
  [[nodiscard]] auto GetNodes() const noexcept
    -> std::span<const pak::world::NodeRecord>
  {
    return nodes_;
  }

  //! Returns the node at the specified index.
  /*!
    @param index The index of the node.
    @return The node record.
    @warning No bounds checking in Release builds. Use GetNodes().size() to
    check.
  */
  OXGN_DATA_NDAPI auto GetNode(pak::world::SceneNodeIndexT index) const noexcept
    -> const pak::world::NodeRecord&;

  //! Returns the name of the specified node.
  /*!
    Resolves the `scene_name_offset` in the node record to a string view from
    the string table.
  */
  OXGN_DATA_NDAPI auto GetNodeName(
    const pak::world::NodeRecord& node) const noexcept -> std::string_view;

  //! The descriptor's string table, for node names and slot parameter strings.
  [[nodiscard]] auto GetStrings() const noexcept -> const DescriptorStrings&
  {
    return strings_;
  }

  //! Returns the first root (index 0). Requires a nonempty GetNodes() span.
  OXGN_DATA_NDAPI auto GetRootNode() const noexcept
    -> const pak::world::NodeRecord&;

  //=== Scene Composition and Start State ===-------------------------------//

  //! True when the scene is a model import's output, which authored scenes
  //! instance and which is never run on its own.
  [[nodiscard]] auto IsModelScene() const noexcept -> bool
  {
    return (desc_.flags & pak::world::SceneAssetFlags::kModelScene)
      != pak::world::SceneAssetFlags::kNone;
  }

  //! Returns the camera node the scene starts viewing through, if authored:
  //! one of its own nodes, or a node an instance brings in.
  [[nodiscard]] auto GetStartingCamera() const noexcept
    -> std::optional<pak::world::NodeRef>
  {
    if (desc_.starting_camera.node_index == pak::world::kNoStartingCamera) {
      return std::nullopt;
    }
    return desc_.starting_camera;
  }

  //! The node IDs of the path `ref` names below its instance root, shallowest
  //! first; empty for one of the scene's own nodes.
  OXGN_DATA_NDAPI auto GetNodePath(const pak::world::NodeRef& ref) const
    -> std::vector<NodeId>;

  //=== Environment Access (v3+) ===----------------------------------------//

  //! The header of the environment block that ends the descriptor.
  [[nodiscard]] auto GetEnvironmentBlockHeader() const noexcept
    -> const pak::world::SceneEnvironmentBlockHeader&
  {
    return environment_block_header_;
  }

  //! Returns a stable view over environment-system records.
  OXGN_DATA_NDAPI auto GetEnvironmentSystemRecords() const noexcept
    -> std::span<const EnvironmentSystemRecordView>
  {
    return environment_system_records_;
  }

  // Typed environment access (v3+). These return structs as defined in the
  // PAK format, not runtime Scene objects.
  [[nodiscard]] auto TryGetSkyAtmosphereEnvironment() const noexcept
    -> std::optional<pak::world::SkyAtmosphereEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::SkyAtmosphereEnvironmentRecord>();
  }

  [[nodiscard]] auto TryGetVolumetricCloudsEnvironment() const noexcept
    -> std::optional<pak::world::VolumetricCloudsEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::VolumetricCloudsEnvironmentRecord>();
  }

  [[nodiscard]] auto TryGetFogEnvironment() const noexcept
    -> std::optional<pak::world::FogEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::FogEnvironmentRecord>();
  }

  [[nodiscard]] auto TryGetSkyLightEnvironment() const noexcept
    -> std::optional<pak::world::SkyLightEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::SkyLightEnvironmentRecord>();
  }

  [[nodiscard]] auto TryGetSkySphereEnvironment() const noexcept
    -> std::optional<pak::world::SkySphereEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::SkySphereEnvironmentRecord>();
  }

  [[nodiscard]] auto TryGetPostProcessVolumeEnvironment() const noexcept
    -> std::optional<pak::world::PostProcessVolumeEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::PostProcessVolumeEnvironmentRecord>();
  }

  //! Curve keys decoded from the current post-process record's bounded tail.
  [[nodiscard]] auto GetPostProcessCompensationCurve() const noexcept
    -> std::span<const pak::world::ExposureCompensationKeyRecord>
  {
    return post_process_curve_;
  }

  //! Returns the authored display background when present.
  [[nodiscard]] auto TryGetBackgroundEnvironment() const noexcept
    -> std::optional<pak::world::BackgroundEnvironmentRecord>
  {
    return EnvironmentRecord<pak::world::BackgroundEnvironmentRecord>();
  }

  //=== Component Access ===--------------------------------------------------//

  //! Returns the records of the component table of type `T`, empty when the
  //! scene has no such table.
  template <typename T>
  [[nodiscard]] auto GetComponents() const noexcept -> std::span<const T>
  {
    return std::get<std::vector<T>>(component_tables_);
  }

  //! The component of type `T` of the node `node_index`, if it has one.
  template <NodeComponentRecord T>
  [[nodiscard]] auto FindComponent(
    pak::world::SceneNodeIndexT node_index) const noexcept -> const T*
  {
    // The reader admits node tables only in strict node order.
    const auto components = GetComponents<T>();
    const auto it
      = std::ranges::lower_bound(components, node_index, std::ranges::less {},
        [](const T& record) -> auto { return record.node_index; });
    if (it != components.end() && it->node_index == node_index) {
      return &(*it);
    }
    return nullptr;
  }

private:
  template <typename T>
  [[nodiscard]] auto EnvironmentRecord() const noexcept -> std::optional<T>
  {
    return std::get<std::optional<T>>(environment_records_);
  }

  //=== Admission, in reading order ===------------------------------------//

  //! Runs every step below, each relying on those before it.
  auto ParseAndValidate() -> void;
  auto ReadHeader() -> void;
  auto ReadStrings() -> void;
  auto ReadNodes() -> void;
  auto ValidateHierarchy() const -> void;
  auto ReadComponentTables() -> void;
  auto ValidateNodeTables() const -> void;
  auto ValidateComponentOwners() const -> void;
  auto ReadScriptTables() -> void;
  auto ReadEnvironment() -> void;

  // Validates node IDs, node paths and every node reference.
  auto ValidateNodeIdentity() const -> void;
  auto ValidateNodeRef(const pak::world::NodeRef& ref,
    const std::unordered_set<pak::world::SceneNodeIndexT>& instance_roots,
    std::string_view what) const -> void;
  auto ValidateScriptNodes(
    const std::unordered_set<pak::world::SceneNodeIndexT>& instance_roots) const
    -> void;

  // Validates every override against the property its id names.
  auto ValidateOverrides(
    const std::unordered_set<pak::world::SceneNodeIndexT>& instance_roots) const
    -> void;
  auto ValidateOverrideValue(const schema::OverrideProperty& property,
    const pak::world::OverrideValue& value,
    std::vector<bool>& lod_policy_used) const -> void;

  std::shared_ptr<std::vector<std::byte>> owned_data_;
  std::span<const std::byte> data_;
  pak::world::SceneAssetDesc desc_ {};

  //! Where the bytes the descriptor's tables, slots and parameters hold end:
  //! the start of the environment block.
  std::size_t payload_end_ { 0U };

  DescriptorStrings strings_;
  std::vector<pak::world::NodeRecord> nodes_;
  detail::SceneRecordStorage<SceneComponentRecords>::Tables component_tables_;

  pak::world::SceneEnvironmentBlockHeader environment_block_header_ {};
  std::vector<EnvironmentSystemRecordView> environment_system_records_;
  detail::SceneRecordStorage<SceneEnvironmentRecords>::Optionals
    environment_records_;
  std::vector<pak::world::ExposureCompensationKeyRecord> post_process_curve_;
};

} // namespace oxygen::data
