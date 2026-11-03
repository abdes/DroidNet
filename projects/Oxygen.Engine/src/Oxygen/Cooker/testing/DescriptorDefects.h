//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::cooker::testing {

//! Named defects applied to a valid built descriptor.
/*!
 Each defect breaks one rule of its format, expressed against the descriptor
 structs, so a test of that rule keeps testing it after a format change. A
 defect records the descriptor's new content hash, except a defect of that
 hash.

 A record defect (`SetNode`, `SetComponent`, `SetScriptSlot`, `SetBinding`)
 writes a record a test read with the matching `*At` and changed in one field; a
 defect of a rule between records, such as their order, writes two.

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

  //! Clears the content hash of the descriptor's header.
  static auto ClearContentHash(std::vector<std::byte>& descriptor) -> void;

  //! Changes the content hash of the descriptor's header.
  static auto CorruptContentHash(std::vector<std::byte>& descriptor) -> void;

  //! Sets the format version of the descriptor's header.
  static auto SetVersion(std::vector<std::byte>& descriptor, uint8_t version)
    -> void;

  //! Replaces the string `from` of the descriptor's string table with `to`,
  //! of the same length, so no offset moves.
  /*!
   @throw std::invalid_argument When the lengths differ or the table holds no
   string `from`.
  */
  static auto ReplaceString(std::vector<std::byte>& descriptor,
    std::string_view from, std::string_view to) -> void;

  //=== Input ===-----------------------------------------------------------//

  //! The input action's descriptor record.
  [[nodiscard]] static auto InputActionAt(std::span<const std::byte> action)
    -> data::pak::input::InputActionAssetDesc;

  //! Replaces the input action's descriptor record, whatever values it holds.
  static auto SetInputAction(std::vector<std::byte>& action,
    const data::pak::input::InputActionAssetDesc& desc) -> void;

  //! The input mapping context's descriptor record.
  [[nodiscard]] static auto InputContextAt(std::span<const std::byte> context)
    -> data::pak::input::InputMappingContextAssetDesc;

  //! Replaces the input mapping context's descriptor record, whatever values
  //! it holds, such as table sizes or flags.
  static auto SetInputContext(std::vector<std::byte>& context,
    const data::pak::input::InputMappingContextAssetDesc& desc) -> void;

  //! The mapping `index` of the input mapping context.
  /*!
   @throw std::invalid_argument When the context has no such mapping.
  */
  [[nodiscard]] static auto InputMappingAt(std::span<const std::byte> context,
    uint32_t index) -> data::pak::input::InputActionMappingRecord;

  //! Replaces the mapping `index` of the input mapping context.
  static auto SetInputMapping(std::vector<std::byte>& context, uint32_t index,
    const data::pak::input::InputActionMappingRecord& mapping) -> void;

  //! The trigger `index` of the input mapping context, counting the triggers
  //! of every mapping in the order the context lists them.
  /*!
   @throw std::invalid_argument When the context has no such trigger.
  */
  [[nodiscard]] static auto InputTriggerAt(std::span<const std::byte> context,
    uint32_t index) -> data::pak::input::InputTriggerRecord;

  //! Replaces the trigger `index` of the input mapping context.
  static auto SetInputTrigger(std::vector<std::byte>& context, uint32_t index,
    const data::pak::input::InputTriggerRecord& trigger) -> void;

  //! The combo step `index` of the input mapping context.
  /*!
   @throw std::invalid_argument When the context has no such step.
  */
  [[nodiscard]] static auto InputTriggerAuxAt(
    std::span<const std::byte> context, uint32_t index)
    -> data::pak::input::InputTriggerAuxRecord;

  //! Replaces the combo step `index` of the input mapping context.
  static auto SetInputTriggerAux(std::vector<std::byte>& context,
    uint32_t index, const data::pak::input::InputTriggerAuxRecord& aux) -> void;

  //=== Materials ===-------------------------------------------------------//

  //! The material's descriptor record.
  [[nodiscard]] static auto MaterialAt(std::span<const std::byte> material)
    -> data::pak::render::MaterialAssetDesc;

  //! Replaces the material's descriptor record.
  static auto SetMaterial(std::vector<std::byte>& material,
    const data::pak::render::MaterialAssetDesc& desc) -> void;

  //! The shader reference `index` of the material, in stage order.
  [[nodiscard]] static auto ShaderReferenceAt(
    std::span<const std::byte> material, uint32_t index)
    -> data::pak::render::ShaderReferenceDesc;

  //! Replaces the shader reference `index` of the material.
  static auto SetShaderReference(std::vector<std::byte>& material,
    uint32_t index, const data::pak::render::ShaderReferenceDesc& shader)
    -> void;

  //=== Geometries ===------------------------------------------------------//

  //! Makes the surface `surface` nil wherever the geometry names it: in its
  //! submeshes and its surface defaults.
  /*!
   @throw std::invalid_argument When the geometry does not name `surface`.
  */
  static auto ClearSurface(
    std::vector<std::byte>& geometry, const data::SurfaceId& surface) -> void;

  //! The mesh view `index` of the geometry, counting the views of every LOD
  //! in the order the descriptor lists them.
  /*!
   @throw std::invalid_argument When the geometry has no such view.
  */
  [[nodiscard]] static auto MeshViewAt(std::span<const std::byte> geometry,
    uint32_t index) -> data::pak::geometry::MeshViewDesc;

  //! Sets the mesh view `index` of the geometry.
  static auto SetMeshView(std::vector<std::byte>& geometry, uint32_t index,
    const data::pak::geometry::MeshViewDesc& view) -> void;

  //! The surface defaults `index` of the geometry's surface table.
  /*!
   @throw std::invalid_argument When the table has no such entry.
  */
  [[nodiscard]] static auto SurfaceDefaultsAt(
    std::span<const std::byte> geometry, uint32_t index)
    -> data::pak::geometry::SurfaceDesc;

  //! Sets the surface defaults `index` of the geometry's surface table.
  static auto SetSurfaceDefaults(std::vector<std::byte>& geometry,
    uint32_t index, const data::pak::geometry::SurfaceDesc& surface) -> void;

  //! Removes the last entry of the geometry's surface table, so it declares
  //! the defaults of one surface fewer.
  /*!
   @throw std::invalid_argument When the geometry declares no defaults.
  */
  static auto DropSurfaceDefaults(std::vector<std::byte>& geometry) -> void;

  //! Sets the geometry's default LOD policy, whatever rules it breaks.
  static auto SetDefaultLodPolicy(std::vector<std::byte>& geometry,
    const data::pak::geometry::LodPolicyRecord& policy) -> void;

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
    data::StampDescriptorHash(scene);
  }

  //! Records `type`, whatever value it holds, as the component type of the
  //! scene's component table `table`.
  static auto SetComponentTableType(std::vector<std::byte>& scene,
    data::ComponentType table, uint32_t type) -> void;

  //! Records `size` as the entry size of the scene's component table `type`.
  static auto SetComponentEntrySize(std::vector<std::byte>& scene,
    data::ComponentType type, uint32_t size) -> void;

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

  //! Records `type`, whatever value it holds, as the system type of the
  //! scene's environment record of type `record`.
  static auto SetEnvironmentRecordType(std::vector<std::byte>& scene,
    data::pak::world::EnvironmentComponentType record, uint32_t type) -> void;

  //! Records `count` as the number of the scene's environment records,
  //! leaving the records as they are.
  static auto SetEnvironmentRecordCount(
    std::vector<std::byte>& scene, uint32_t count) -> void;

  //! Removes the scene's environment block, which ends the descriptor.
  static auto DropEnvironmentBlock(std::vector<std::byte>& scene) -> void;

  //=== Physics sidecars ===------------------------------------------------//

  //! Makes the sidecar name the scene `scene` instead of its own.
  static auto SetSidecarScene(
    std::vector<std::byte>& sidecar, const data::AssetKey& scene) -> void;

  //! Makes the sidecar expect `count` nodes in its scene.
  static auto SetSidecarNodeCount(
    std::vector<std::byte>& sidecar, uint32_t count) -> void;

  //! Records `backend` as the backend the sidecar was cooked for.
  static auto SetSidecarBackend(std::vector<std::byte>& sidecar,
    data::pak::physics::PhysicsBackend backend) -> void;

  //! Changes the content hash of its scene the sidecar records.
  static auto CorruptSidecarSceneHash(std::vector<std::byte>& sidecar) -> void;

  //! Records `size` as the entry size of the sidecar's binding table `type`.
  static auto SetBindingEntrySize(std::vector<std::byte>& sidecar,
    data::pak::physics::PhysicsBindingType type, uint32_t size) -> void;

  //! Records `type`, whatever value it holds, as the binding type of the
  //! sidecar's binding table `table`.
  static auto SetBindingTableType(std::vector<std::byte>& sidecar,
    data::pak::physics::PhysicsBindingType table, uint32_t type) -> void;

  //! The record `index` of the sidecar's table of `Record` bindings.
  template <typename Record>
  [[nodiscard]] static auto BindingAt(
    std::span<const std::byte> sidecar, uint32_t index) -> Record
  {
    auto record = Record {};
    const auto at = BindingOffset(
      sidecar, data::BindingTraits<Record>::kType, index, sizeof(Record));
    std::memcpy(&record, sidecar.subspan(at).data(), sizeof(Record));
    return record;
  }

  //! Replaces the record `index` of the sidecar's table of `Record` bindings.
  template <typename Record>
  static auto SetBinding(std::vector<std::byte>& sidecar, uint32_t index,
    const Record& record) -> void
  {
    const auto at = BindingOffset(
      sidecar, data::BindingTraits<Record>::kType, index, sizeof(Record));
    std::memcpy(std::span(sidecar).subspan(at).data(), &record, sizeof(Record));
    data::StampDescriptorHash(sidecar);
  }

private:
  //! The offset of the mesh view `view` of the geometry or, without a view,
  //! the end of its LOD records, where its surface table starts.
  /*!
   @throw std::invalid_argument When the geometry has no such view.
  */
  [[nodiscard]] static auto LodRecordOffset(std::span<const std::byte> geometry,
    std::optional<uint32_t> view) -> size_t;

  //! The offset of the entry `index` of the geometry's surface table.
  /*!
   @throw std::invalid_argument When the table has no such entry.
  */
  [[nodiscard]] static auto SurfaceDefaultsOffset(
    std::span<const std::byte> geometry, uint32_t index) -> size_t;

  //! The offset of the record `index`, of `size` bytes, of the component
  //! table `type`.
  /*!
   @throw std::invalid_argument When the scene has no such record.
  */
  [[nodiscard]] static auto RecordOffset(std::span<const std::byte> scene,
    data::ComponentType type, uint32_t index, size_t size) -> size_t;

  //! The offset of the directory entry of the scene's component table `type`.
  /*!
   @throw std::invalid_argument When the scene has no such table.
  */
  [[nodiscard]] static auto TableEntryOffset(
    std::span<const std::byte> scene, data::ComponentType type) -> size_t;

  //! The offset of the record `index`, of `size` bytes, of the sidecar's
  //! binding table `type`; a soft body's vertex lists follow its record.
  /*!
   @throw std::invalid_argument When the sidecar has no such record.
  */
  [[nodiscard]] static auto BindingOffset(std::span<const std::byte> sidecar,
    data::pak::physics::PhysicsBindingType type, uint32_t index, size_t size)
    -> size_t;

  //! The offset of the directory entry of the sidecar's binding table `type`.
  /*!
   @throw std::invalid_argument When the sidecar has no such table.
  */
  [[nodiscard]] static auto BindingEntryOffset(
    std::span<const std::byte> sidecar,
    data::pak::physics::PhysicsBindingType type) -> size_t;
};

} // namespace oxygen::cooker::testing
