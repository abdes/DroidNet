//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::cooker::testing {

//! Named defects applied to a valid built descriptor.
/*!
 Each defect breaks one rule of its format, expressed against the descriptor
 structs, so a test of that rule keeps testing it after a format change. A
 defect leaves the header's content hash as it was; no reader checks it.

 A record defect (`SetNode`, `SetComponent`, `SetScriptSlot`) writes a record
 a test read with the matching `*At` and changed in one field; a defect of a
 rule between records, such as their order, writes two.

 ```cpp
 auto scene = SceneBuilder { "Broken" };
 scene.Node("root");
 auto descriptor = scene.Bytes();
 DescriptorDefects::PointNodeTablePastTheEnd(descriptor);
 ```
*/
struct DescriptorDefects final {
  //=== Any descriptor ===--------------------------------------------------//

  //! Sets the asset type of the descriptor's header.
  static auto SetAssetType(
    std::vector<std::byte>& descriptor, data::AssetType type) -> void;

  //! Sets the format version of the descriptor's header.
  static auto SetVersion(std::vector<std::byte>& descriptor, uint8_t version)
    -> void;

  //=== Geometries ===------------------------------------------------------//

  //! Makes the surface `surface` nil wherever the geometry names it: in its
  //! submeshes and its surface defaults.
  /*!
   @throw std::invalid_argument When the geometry does not name `surface`.
  */
  static auto ClearSurface(
    std::vector<std::byte>& geometry, const data::SurfaceId& surface) -> void;

  //=== Scenes ===----------------------------------------------------------//

  //! Makes the node table run past the end of the descriptor.
  static auto PointNodeTablePastTheEnd(std::vector<std::byte>& scene) -> void;

  //! Sets the scene's flags to `flags`, whatever bits they hold.
  static auto SetSceneFlags(std::vector<std::byte>& scene, uint32_t flags)
    -> void;

  //! Sets the scene's starting camera, whatever node it names.
  static auto SetStartingCamera(
    std::vector<std::byte>& scene, data::pak::world::NodeRef camera) -> void;

  //! The node `index` of the scene.
  [[nodiscard]] static auto NodeAt(std::span<const std::byte> scene,
    uint32_t index) -> data::pak::world::NodeRecord;

  //! Replaces the node `index` of the scene.
  static auto SetNode(std::vector<std::byte>& scene, uint32_t index,
    const data::pak::world::NodeRecord& node) -> void;

  //! The record `index` of the scene's component table `type`.
  template <typename Record>
  [[nodiscard]] static auto ComponentAt(std::span<const std::byte> scene,
    data::ComponentType type, uint32_t index) -> Record
  {
    auto record = Record {};
    const auto at = RecordOffset(scene, type, index, sizeof(Record));
    std::memcpy(&record, scene.subspan(at).data(), sizeof(Record));
    return record;
  }

  //! Replaces the record `index` of the scene's component table `type`.
  template <typename Record>
  static auto SetComponent(std::vector<std::byte>& scene,
    data::ComponentType type, uint32_t index, const Record& record) -> void
  {
    const auto at = RecordOffset(scene, type, index, sizeof(Record));
    std::memcpy(std::span(scene).subspan(at).data(), &record, sizeof(Record));
  }

  //! The script slot `index` of the scene.
  [[nodiscard]] static auto ScriptSlotAt(std::span<const std::byte> scene,
    uint32_t index) -> data::pak::scripting::ScriptSlotRecord;

  //! Replaces the script slot `index` of the scene.
  static auto SetScriptSlot(std::vector<std::byte>& scene, uint32_t index,
    const data::pak::scripting::ScriptSlotRecord& slot) -> void;

  //! The bytes of the scene's environment record of type `type`, its header
  //! and what its size includes, to damage in place.
  [[nodiscard]] static auto EnvironmentRecord(std::vector<std::byte>& scene,
    data::pak::world::EnvironmentComponentType type) -> std::span<std::byte>;

  //! Records `size` as the size of the scene's environment record of type
  //! `type`, leaving its bytes as they are.
  static auto SetEnvironmentRecordSize(std::vector<std::byte>& scene,
    data::pak::world::EnvironmentComponentType type, uint32_t size) -> void;

  //=== Physics sidecars ===------------------------------------------------//

  //! Makes the sidecar name the scene `scene` instead of its own.
  static auto SetSidecarScene(
    std::vector<std::byte>& sidecar, const data::AssetKey& scene) -> void;

  //! Makes the sidecar expect `count` nodes in its scene.
  static auto SetSidecarNodeCount(
    std::vector<std::byte>& sidecar, uint32_t count) -> void;

  //! Changes the content hash of its scene the sidecar records.
  static auto CorruptSidecarSceneHash(std::vector<std::byte>& sidecar) -> void;

  //! Records `size` as the entry size of the sidecar's binding table `type`.
  static auto SetBindingEntrySize(std::vector<std::byte>& sidecar,
    data::pak::physics::PhysicsBindingType type, uint32_t size) -> void;

private:
  //! The offset of the record `index`, of `size` bytes, of the component
  //! table `type`.
  /*!
   @throw std::invalid_argument When the scene has no such record.
  */
  [[nodiscard]] static auto RecordOffset(std::span<const std::byte> scene,
    data::ComponentType type, uint32_t index, size_t size) -> size_t;
};

} // namespace oxygen::cooker::testing
