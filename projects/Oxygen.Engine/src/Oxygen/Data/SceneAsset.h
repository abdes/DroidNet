//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::data {

//=== Component Traits & Type List ===---------------------------------------//

//! Traits class to map component record types to their ComponentType enum.
template <typename T> struct ComponentTraits;

template <> struct ComponentTraits<pak::RenderableRecord> {
  static constexpr ComponentType kType = ComponentType::kRenderable;
};

template <> struct ComponentTraits<pak::PerspectiveCameraRecord> {
  static constexpr ComponentType kType = ComponentType::kPerspectiveCamera;
};

template <> struct ComponentTraits<pak::OrthographicCameraRecord> {
  static constexpr ComponentType kType = ComponentType::kOrthographicCamera;
};

template <> struct ComponentTraits<pak::DirectionalLightRecord> {
  static constexpr ComponentType kType = ComponentType::kDirectionalLight;
};

template <> struct ComponentTraits<pak::PointLightRecord> {
  static constexpr ComponentType kType = ComponentType::kPointLight;
};

template <> struct ComponentTraits<pak::SpotLightRecord> {
  static constexpr ComponentType kType = ComponentType::kSpotLight;
};

//! Represents a loaded Scene asset.
/*!
  SceneAsset provides a high-level, read-only view over the raw binary data of a
  cooked scene asset (SceneAssetDesc). It handles the details of navigating the
  node hierarchy, string tables, and component directories.

  ### Zero-Copy Design
  This class is designed to be a lightweight wrapper around the raw data blob.
  It does not copy the data, but rather provides views (std::span) into it.
  The raw data must remain valid for the lifetime of this object.

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
  auto renderables = scene.GetComponents<pak::RenderableRecord>();
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
    pak::SceneEnvironmentSystemRecordHeader header {};
    std::span<const std::byte> bytes {};
  };

  //! Constructs a SceneAsset from a raw data blob.
  /*!
    @param key The stable asset key.
    @param data The raw binary data of the scene asset (must start with
    SceneAssetDesc).
    @throws std::runtime_error if the data is invalid or too small.
  */
  OXGN_DATA_API SceneAsset(AssetKey key, std::span<const std::byte> data);

  //! Constructs a SceneAsset that owns its raw data.
  /*!
    This is the preferred construction path for loaders.

    @param key The stable asset key.
    @param data The raw binary data of the scene asset (must start with
      SceneAssetDesc).
    @throws std::runtime_error if the data is invalid or too small.
  */
  OXGN_DATA_API SceneAsset(AssetKey key, std::vector<std::byte> data);

  ~SceneAsset() override = default;

  OXYGEN_DEFAULT_COPYABLE(SceneAsset)
  OXYGEN_DEFAULT_MOVABLE(SceneAsset)

  //=== Asset Interface ===---------------------------------------------------//

  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::AssetHeader& override
  {
    return desc_.header;
  }

  //=== Node Access ===-------------------------------------------------------//

  //! Returns a view of all nodes in the scene.
  OXGN_DATA_NDAPI auto GetNodes() const noexcept
    -> std::span<const pak::NodeRecord>;

  //! Returns the node at the specified index.
  /*!
    @param index The index of the node.
    @return The node record.
    @warning No bounds checking in Release builds. Use GetNodes().size() to
    check.
  */
  OXGN_DATA_NDAPI auto GetNode(pak::SceneNodeIndexT index) const noexcept
    -> const pak::NodeRecord&;

  //! Returns the name of the specified node.
  /*!
    Resolves the `scene_name_offset` in the node record to a string view from
    the string table.
  */
  OXGN_DATA_NDAPI auto GetNodeName(const pak::NodeRecord& node) const noexcept
    -> std::string_view;

  //! Returns the root node (always index 0).
  OXGN_DATA_NDAPI auto GetRootNode() const noexcept -> const pak::NodeRecord&;

  //=== Environment Access (v3+) ===----------------------------------------//

  //! Returns true if this scene carries a trailing environment block.
  OXGN_DATA_NDAPI auto HasEnvironmentBlock() const noexcept -> bool
  {
    return has_environment_block_;
  }

  //! Gets the parsed environment block header (if present).
  OXGN_DATA_NDAPI auto GetEnvironmentBlockHeader() const noexcept
    -> const pak::SceneEnvironmentBlockHeader*
  {
    return has_environment_block_ ? &environment_block_header_ : nullptr;
  }

  //! Returns a stable view over environment-system records.
  OXGN_DATA_NDAPI auto GetEnvironmentSystemRecords() const noexcept
    -> std::span<const EnvironmentSystemRecordView>
  {
    return environment_system_records_;
  }

  // Typed environment access (v3+). These return structs as defined in the
  // PAK format, not runtime Scene objects.
  OXGN_DATA_NDAPI auto TryGetSkyAtmosphereEnvironment() const
    -> std::optional<pak::SkyAtmosphereEnvironmentRecord>;

  OXGN_DATA_NDAPI auto TryGetVolumetricCloudsEnvironment() const
    -> std::optional<pak::VolumetricCloudsEnvironmentRecord>;

  OXGN_DATA_NDAPI auto TryGetFogEnvironment() const
    -> std::optional<pak::FogEnvironmentRecord>;

  OXGN_DATA_NDAPI auto TryGetSkyLightEnvironment() const
    -> std::optional<pak::SkyLightEnvironmentRecord>;

  OXGN_DATA_NDAPI auto TryGetSkySphereEnvironment() const
    -> std::optional<pak::SkySphereEnvironmentRecord>;

  OXGN_DATA_NDAPI auto TryGetPostProcessVolumeEnvironment() const
    -> std::optional<pak::PostProcessVolumeEnvironmentRecord>;

  //=== Component Access ===--------------------------------------------------//

  //! Returns a view of all components of the specified type.
  /*!
    @tparam T The component record type (e.g. pak::RenderableRecord).
    @return A span of component records. Returns empty span if no table exists.
  */
  template <typename T>
  [[nodiscard]] auto GetComponents() const noexcept -> std::span<const T>
  {
    // Map C++ type to ComponentType enum using Traits.
    constexpr ComponentType type = ComponentTraits<T>::kType;
    const auto* entry = FindComponentTableEntry(type);
    if (entry == nullptr || entry->count == 0) {
      return {};
    }
    if (entry->entry_size != sizeof(T)) {
      DCHECK_F(false,
        "SceneAsset component table entry size mismatch (validated by "
        "loader)");
      return {};
    }

    // Component tables are stored packed (alignment 1). Exposing them via
    // unaligned typed pointers is UB in Release builds. Decode field-by-field
    // into an aligned cache and return a stable view.
    const auto it = component_cache_.find(type);
    if (it == component_cache_.end()) {
      const size_t bytes_size = entry->count * sizeof(T);
      const auto bytes = data_.subspan(entry->offset, bytes_size);

      std::vector<std::byte> buffer;
      buffer.assign(bytes.begin(), bytes.end());

      oxygen::serio::MemoryStream stream { std::span<std::byte>(buffer) };
      oxygen::serio::Reader<oxygen::serio::MemoryStream> reader(stream);
      auto pack = reader.ScopedAlignment(1);

      std::vector<T> decoded;
      decoded.resize(entry->count);
      for (size_t i = 0; i < entry->count; ++i) {
        const auto res = reader.ReadInto(decoded[i]);
        if (!res) {
          DCHECK_F(false,
            "SceneAsset failed to deserialize component table (validated by "
            "loader)");
          return {};
        }
      }

      component_cache_.insert_or_assign(type, std::move(decoded));
    }

    const auto& cached = std::any_cast<const std::vector<T>&>(
      component_cache_.find(type)->second);
    return { cached.data(), cached.size() };
  }

  //! Finds the component of type T attached to the specified node.
  /*!
    @tparam T The component record type.
    @param node_index The index of the node to find the component for.
    @return Pointer to the component record, or nullptr if not found.

    @note Assumes component tables are sorted by node_index (as per spec).
    Uses binary search for O(log N) lookup.
  */
  template <typename T>
  [[nodiscard]] auto FindComponent(
    pak::SceneNodeIndexT node_index) const noexcept -> const T*
  {
    auto components = GetComponents<T>();

    // Binary search for the node_index
    auto it = std::lower_bound(components.begin(), components.end(), node_index,
      [](const T& record, pak::SceneNodeIndexT index) {
        return record.node_index < index;
      });

    if (it != components.end() && it->node_index == node_index) {
      return &(*it);
    }
    return nullptr;
  }

private:
  struct ComponentTableEntry;

  // Finds a component table entry for the given component type.
  [[nodiscard]] auto FindComponentTableEntry(ComponentType type) const noexcept
    -> const ComponentTableEntry*
  {
    for (const auto& entry : component_tables_) {
      if (entry.type == type) {
        return &entry;
      }
    }
    return nullptr;
  }

  // Validates offsets and sizes against the data buffer
  auto ParseAndValidate() -> void;

  std::shared_ptr<std::vector<std::byte>> owned_data_ {};
  std::span<const std::byte> data_;
  pak::SceneAssetDesc desc_ {};

  // Cached pointers for fast access
  size_t node_count_ { 0 };

  // Nodes are stored packed (alignment 1). Decode lazily into an aligned
  // cache to avoid unaligned typed pointers.
  mutable std::vector<pak::NodeRecord> nodes_cache_ {};
  mutable bool nodes_cache_valid_ { false };

  const char* string_table_ptr_ { nullptr };
  size_t string_table_size_ { 0 };

  // Component directory cache (type -> {ptr, count})
  struct ComponentTableEntry {
    ComponentType type;
    pak::OffsetT offset;
    size_t count;
    uint32_t entry_size;
  };
  std::vector<ComponentTableEntry> component_tables_;

  // Optional trailing environment block.
  bool has_environment_block_ { false };
  pak::SceneEnvironmentBlockHeader environment_block_header_ {};
  std::vector<EnvironmentSystemRecordView> environment_system_records_;

  // Decoded component tables, keyed by ComponentType.
  mutable std::unordered_map<ComponentType, std::any> component_cache_ {};

  template <typename RecordT>
  auto TryGetEnvironmentRecordAs(pak::EnvironmentComponentType type) const
    -> std::optional<RecordT>;
};

} // namespace oxygen::data
