//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

// Forward declaration of BindingTraits (specializations follow after the class)
template <typename T> struct BindingTraits;

// Hash functor for PhysicsBindingType (scoped enum over uint32_t)
struct PhysicsBindingTypeHash {
  size_t operator()(pak::physics::PhysicsBindingType bt) const noexcept
  {
    return std::hash<uint32_t> {}(static_cast<uint32_t>(bt));
  }
};

//! Represents a loaded Physics Scene sidecar asset.
/*!
  PhysicsSceneAsset provides a read-only view over the raw binary data of a
  cooked physics sidecar asset (PhysicsSceneAssetDesc). It handles the details
  of navigating the binding table directory without copying the data.

  ### Domain Separation
  Physics binding records are NOT scene node components. They are sidecar
  domain bindings that reference scene nodes by index but live entirely outside
  the Scene's composition architecture. This asset is always loaded as a
  companion to a SceneAsset but is owned and managed independently.

  ### Zero-Copy Design
  This class is a lightweight wrapper around the raw data blob. It does not
  copy the data; it provides views (std::span) into it. The raw data must
  remain valid for the lifetime of this object.

  ### Usage
  ```cpp
  // Constructed by PhysicsSceneLoader; held by AssetLoader cache.
  auto rigid_bodies = asset.GetBindings<pak::physics::RigidBodyBindingRecord>();
  for (const auto& rec : rigid_bodies) {
      // rec.node_index -> paired SceneAsset node
      // rec.body_type, rec.shape_asset_key, etc.
  }
  ```
*/
class PhysicsSceneAsset final : public Asset {
  OXYGEN_TYPED(PhysicsSceneAsset)
public:
  //! Constructs a PhysicsSceneAsset from a borrowed span (non-owning).
  OXGN_DATA_API PhysicsSceneAsset(
    AssetKey key, std::span<const std::byte> data);

  //! Constructs a PhysicsSceneAsset that owns the raw data blob.
  //! This is the preferred path for loaders.
  OXGN_DATA_API PhysicsSceneAsset(AssetKey key, std::vector<std::byte> data);

  ~PhysicsSceneAsset() override = default;

  OXYGEN_DEFAULT_COPYABLE(PhysicsSceneAsset)
  OXYGEN_DEFAULT_MOVABLE(PhysicsSceneAsset)

  //=== Asset Interface ===---------------------------------------------------//

  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::core::AssetHeader& override
  {
    return desc_.header;
  }

  //=== Identity / Pairing ===------------------------------------------------//

  //! Returns the key of the Scene asset this sidecar is paired with.
  [[nodiscard]] auto GetTargetSceneKey() const noexcept -> const AssetKey&
  {
    return desc_.target_scene_key;
  }

  //! Returns the expected node count for identity validation.
  [[nodiscard]] auto GetTargetNodeCount() const noexcept -> uint32_t
  {
    return desc_.target_node_count;
  }

  //=== Binding Table Access ===----------------------------------------------//

  //! Returns a view of all binding records of the specified physics type.
  /*!
    @tparam T  A binding record type (e.g.
    pak::physics::RigidBodyBindingRecord). The mapping to PhysicsBindingType is
    via BindingTraits<T>.
    @return    Span of decoded binding records. Returns empty span if absent.
  */
  template <typename T>
  [[nodiscard]] auto GetBindings() const noexcept -> std::span<const T>
  {
    constexpr pak::physics::PhysicsBindingType type = BindingTraits<T>::kType;
    const BindingTableEntry* entry = FindBindingTableEntry(type);
    if (entry == nullptr || entry->count == 0) {
      return {};
    }
    if (entry->entry_size != sizeof(T)) {
      DCHECK_F(false,
        "PhysicsSceneAsset binding table entry size mismatch (validated by "
        "loader)");
      return {};
    }

    // Binding tables are stored packed (alignment 1).
    // Decode lazily into an aligned cache to avoid unaligned typed pointers.
    const auto it = binding_cache_.find(type);
    if (it == binding_cache_.end()) {
      const size_t bytes_size = entry->count * sizeof(T);
      const auto bytes = data_.subspan(entry->offset, bytes_size);

      std::vector<T> decoded;
      decoded.resize(entry->count);
      for (size_t i = 0; i < entry->count; ++i) {
        std::memcpy(&decoded[i], bytes.subspan(i * sizeof(T), sizeof(T)).data(),
          sizeof(T));
      }

      binding_cache_.insert_or_assign(type, std::move(decoded));
    }

    const auto& cached
      = std::any_cast<const std::vector<T>&>(binding_cache_.find(type)->second);
    return { cached.data(), cached.size() };
  }

  //! Finds the binding record attached to a specific scene node, if any.
  /*!
    Uses binary search (tables are sorted by node_index per spec).
  */
  template <typename T>
  [[nodiscard]] auto FindBinding(
    pak::world::SceneNodeIndexT node_index) const noexcept -> const T*
  {
    auto bindings = GetBindings<T>();
    auto it = std::lower_bound(bindings.begin(), bindings.end(), node_index,
      [](const T& rec, pak::world::SceneNodeIndexT idx) {
        return rec.node_index < idx;
      });
    if (it != bindings.end() && it->node_index == node_index) {
      return &(*it);
    }
    return nullptr;
  }

private:
  struct BindingTableEntry;

  [[nodiscard]] inline auto FindBindingTableEntry(
    pak::physics::PhysicsBindingType type) const noexcept
    -> const BindingTableEntry*
  {
    for (const auto& entry : binding_tables_) {
      if (entry.type == type) {
        return &entry;
      }
    }
    return nullptr;
  }

  auto ParseAndValidate() -> void;

  std::shared_ptr<std::vector<std::byte>> owned_data_ {};
  std::span<const std::byte> data_;
  pak::physics::PhysicsSceneAssetDesc desc_ {};

  struct BindingTableEntry {
    pak::physics::PhysicsBindingType type;
    pak::core::OffsetT offset;
    size_t count;
    uint32_t entry_size;
  };
  std::vector<BindingTableEntry> binding_tables_;

  mutable std::unordered_map<pak::physics::PhysicsBindingType, std::any,
    PhysicsBindingTypeHash>
    binding_cache_ {};
};

// ---------------------------------------------------------------------------
// BindingTraits — maps C++ record type → PhysicsBindingType enum
// ---------------------------------------------------------------------------
// (Primary template declared before the class; specializations below.)

template <> struct BindingTraits<pak::physics::RigidBodyBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kRigidBody;
};

template <> struct BindingTraits<pak::physics::ColliderBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kCollider;
};

template <> struct BindingTraits<pak::physics::CharacterBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kCharacter;
};

template <> struct BindingTraits<pak::physics::SoftBodyBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kSoftBody;
};

template <> struct BindingTraits<pak::physics::JointBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kJoint;
};

template <> struct BindingTraits<pak::physics::VehicleBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kVehicle;
};

template <> struct BindingTraits<pak::physics::AggregateBindingRecord> {
  static constexpr pak::physics::PhysicsBindingType kType
    = pak::physics::PhysicsBindingType::kAggregate;
};

} // namespace oxygen::data
