//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <tuple>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

class SceneAsset;

//! Maps a binding record type to the binding type of its table.
template <typename T> struct BindingTraits;

template <> struct BindingTraits<pak::physics::RigidBodyBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kRigidBody;
};

template <> struct BindingTraits<pak::physics::ColliderBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kCollider;
};

template <> struct BindingTraits<pak::physics::CharacterBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kCharacter;
};

template <> struct BindingTraits<pak::physics::SoftBodyBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kSoftBody;
};

template <> struct BindingTraits<pak::physics::JointBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kJoint;
};

template <> struct BindingTraits<pak::physics::VehicleBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kVehicle;
};

template <> struct BindingTraits<pak::physics::VehicleWheelBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kVehicleWheel;
};

template <> struct BindingTraits<pak::physics::AggregateBindingRecord> {
  static constexpr auto kType = pak::physics::PhysicsBindingType::kAggregate;
};

//! The record types of the binding tables a physics sidecar holds, one table
//! per type; the reader refuses a table of any other type.
using PhysicsBindingRecords = std::tuple<pak::physics::RigidBodyBindingRecord,
  pak::physics::ColliderBindingRecord, pak::physics::CharacterBindingRecord,
  pak::physics::SoftBodyBindingRecord, pak::physics::JointBindingRecord,
  pak::physics::VehicleBindingRecord, pak::physics::VehicleWheelBindingRecord,
  pak::physics::AggregateBindingRecord>;

//! A binding of one node: tables of them hold one record per node, sorted by
//! node.
template <typename T>
concept NodeBindingRecord = requires(const T& record) { record.node_index; };

//! The vertex lists of a soft body binding, which follow its record.
struct SoftBodyVertexLists {
  //! The particles the soft body pins in place.
  std::vector<uint32_t> pinned;
  //! The particles its animation drives.
  std::vector<uint32_t> kinematic;
};

namespace detail {

  template <typename Records> struct BindingTables;

  template <typename... Records> struct BindingTables<std::tuple<Records...>> {
    using Type = std::tuple<std::vector<Records>...>;
  };

} // namespace detail

//! Represents a loaded Physics Scene sidecar asset.
/*!
  PhysicsSceneAsset is the validating reader of a cooked physics sidecar
  descriptor (PhysicsSceneAssetDesc) and the read-only model of the bindings
  it holds. Construction decodes every binding table and refuses a malformed
  descriptor with a `CookedFormatError`: an unknown or repeated table type, a
  binding of a node its scene does not hold, a node table out of node order,
  wheel slices that do not partition the wheel table, soft body vertex lists
  off their records, and non-zero reserves in the arm of its backend.

  ### Domain Separation
  Physics binding records are NOT scene node components. They are sidecar
  domain bindings that reference scene nodes by index but live entirely outside
  the Scene's composition architecture. This asset is always loaded as a
  companion to a SceneAsset but is owned and managed independently.

  ### Usage
  ```cpp
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
  //! Reads the sidecar descriptor `data`.
  /*!
   @throw CookedFormatError When the descriptor breaks a rule of its format.
  */
  OXGN_DATA_API PhysicsSceneAsset(AssetKey key, std::span<const std::byte> data,
    SourceOrigin source_origin = {});

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

  //! The backend the scene was cooked for, whose arm of each backend-scalar
  //! union its bindings hold.
  [[nodiscard]] auto GetBackend() const noexcept -> pak::physics::PhysicsBackend
  {
    return desc_.backend;
  }

  //! The content hash of the paired scene when this sidecar was cooked.
  [[nodiscard]] auto GetTargetSceneContentHash() const noexcept
    -> std::span<const uint8_t, 32>
  {
    return std::span<const uint8_t, 32> { desc_.target_scene_content_hash };
  }

  //! Checks that `scene` is the scene this sidecar was cooked against: its
  //! key, its node count and its content hash.
  /*!
   @throw CookedFormatError `kInvalidReference` when `scene` has another key,
     `kInconsistent` when it has another node count, and `kDigestMismatch`
     when it has another content hash.
  */
  OXGN_DATA_API auto CheckTarget(const SceneAsset& scene) const -> void;

  //=== Binding Table Access ===----------------------------------------------//

  //! The records of the binding table of type `T`, empty when the sidecar has
  //! no such table.
  template <typename T>
  [[nodiscard]] auto GetBindings() const noexcept -> std::span<const T>
  {
    return std::get<std::vector<T>>(binding_tables_);
  }

  //! The binding of type `T` of the node `node_index`, if it has one.
  template <NodeBindingRecord T>
  [[nodiscard]] auto FindBinding(
    pak::world::SceneNodeIndexT node_index) const noexcept -> const T*
  {
    // The reader admits node tables only in strict node order.
    const auto bindings = GetBindings<T>();
    const auto it
      = std::ranges::lower_bound(bindings, node_index, std::ranges::less {},
        [](const T& record) -> auto { return record.node_index; });
    if (it != bindings.end() && it->node_index == node_index) {
      return &(*it);
    }
    return nullptr;
  }

  //! The vertex lists of the soft body binding `binding`, the index of its
  //! record in `GetBindings<pak::physics::SoftBodyBindingRecord>()`.
  /*!
   @throw std::out_of_range When the sidecar has no such soft body.
  */
  OXGN_DATA_NDAPI auto GetSoftBodyVertices(std::size_t binding) const
    -> const SoftBodyVertexLists&;

private:
  //=== Admission, in reading order ===------------------------------------//

  //! Runs every step below, each relying on those before it.
  auto ParseAndValidate(std::span<const std::byte> data) -> void;
  auto ReadHeader(std::span<const std::byte> data) -> void;
  auto ReadStrings(std::span<const std::byte> data) -> void;
  auto ReadBindingTables(std::span<const std::byte> data) -> void;
  auto ValidateReserves() const -> void;
  auto ValidateNodeTables() const -> void;
  auto ValidateJoints() const -> void;
  auto ValidateWheels() const -> void;

  pak::physics::PhysicsSceneAssetDesc desc_ {};
  detail::BindingTables<PhysicsBindingRecords>::Type binding_tables_;
  //! The vertex lists of each soft body, in the order of their records.
  std::vector<SoftBodyVertexLists> soft_body_vertices_;
};

} // namespace oxygen::data
