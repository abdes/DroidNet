//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Hash.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/SceneSync/api_export.h>

namespace oxygen::scenesync {

struct RuntimeMaterialMotionKey {
  scene::NodeHandle node_handle {};
  data::AssetKey geometry_asset_key {};
  std::uint32_t lod_index { 0U };
  std::uint32_t submesh_index { 0U };

  [[nodiscard]] constexpr auto operator==(
    const RuntimeMaterialMotionKey&) const noexcept -> bool
    = default;
};

struct RuntimeMaterialMotionKeyHash {
  [[nodiscard]] auto operator()(
    const RuntimeMaterialMotionKey& key) const noexcept -> std::size_t
  {
    std::size_t seed = std::hash<scene::NodeHandle> {}(key.node_handle);
    oxygen::HashCombine(seed, key.geometry_asset_key);
    oxygen::HashCombine(seed, key.lod_index);
    oxygen::HashCombine(seed, key.submesh_index);
    return seed;
  }
};

struct MaterialMotionCapabilityFlags {
  bool uses_world_position_offset { false };
  bool uses_motion_vector_world_offset { false };
  bool uses_temporal_responsiveness { false };
  bool has_pixel_animation { false };
};

struct PublishedSkinnedPoseState {
  scene::NodeHandle node_handle {};
  data::AssetKey geometry_asset_key {};
  data::AssetKey skeleton_asset_key {};
  std::uint64_t contract_hash { 0U };
  // TODO(vortex/skinned-morph): replace this placeholder-only bridge field
  // with real frozen joint-palette publication once skeleton/animation runtime
  // ownership exists in the engine.
  bool has_runtime_pose { false };
};

struct PublishedMorphDeformationState {
  scene::NodeHandle node_handle {};
  data::AssetKey geometry_asset_key {};
  std::uint64_t contract_hash { 0U };
  // TODO(vortex/skinned-morph): replace this placeholder-only bridge field
  // with real frozen morph/deformation publication once the engine exposes an
  // authoritative morph runtime.
  bool has_runtime_deformation { false };
};

struct PublishedRuntimeMaterialMotionState {
  RuntimeMaterialMotionKey key {};
  data::AssetKey resolved_material_asset_key {};
  std::uint64_t contract_hash { 0U };
  bool has_runtime_wpo_input { false };
  bool has_runtime_motion_vector_input { false };
  MaterialMotionCapabilityFlags capabilities {};
  std::array<float, 4U> wpo_parameter_block0 {};
  std::array<float, 4U> motion_vector_parameter_block0 {};
};

struct RuntimeMaterialMotionInputState {
  RuntimeMaterialMotionKey key {};
  data::AssetKey resolved_material_asset_key {};
  std::uint64_t contract_hash { 0U };
  bool has_runtime_wpo_input { false };
  bool has_runtime_motion_vector_input { false };
  MaterialMotionCapabilityFlags capabilities {};
  std::array<float, 4U> wpo_parameter_block0 {};
  std::array<float, 4U> motion_vector_parameter_block0 {};
};

struct PublishedRuntimeMotionSnapshot {
  frame::SequenceNumber frame_sequence { 0U };
  std::vector<PublishedSkinnedPoseState> skinned_pose_states {};
  std::vector<PublishedMorphDeformationState> morph_deformation_states {};
  std::vector<PublishedRuntimeMaterialMotionState> material_motion_states {};
  std::unordered_map<RuntimeMaterialMotionKey, std::size_t,
    RuntimeMaterialMotionKeyHash>
    material_motion_index {};

  [[nodiscard]] auto FindMaterialMotionState(
    const RuntimeMaterialMotionKey& key) const noexcept
    -> const PublishedRuntimeMaterialMotionState*
  {
    const auto it = material_motion_index.find(key);
    if (it == material_motion_index.end()
      || it->second >= material_motion_states.size()) {
      return nullptr;
    }
    return &material_motion_states[it->second];
  }
};

class RuntimeMotionProducerModule final : public engine::EngineModule {
  OXYGEN_TYPED(RuntimeMotionProducerModule)

public:
  OXGN_SCNSYNC_API explicit RuntimeMotionProducerModule(
    engine::ModulePriority priority
    = engine::kRuntimeMotionProducerModulePriority);
  OXGN_SCNSYNC_API ~RuntimeMotionProducerModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RuntimeMotionProducerModule)
  OXYGEN_MAKE_NON_MOVABLE(RuntimeMotionProducerModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "RuntimeMotionProducerModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return priority_;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kSceneMutation,
      core::PhaseId::kPublishViews>();
  }

  OXGN_SCNSYNC_API auto OnSceneMutation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;
  OXGN_SCNSYNC_API auto OnPublishViews(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;
  OXGN_SCNSYNC_API auto OnShutdown() noexcept -> void override;

  [[nodiscard]] OXGN_SCNSYNC_API auto GetPublishedSnapshot(
    observer_ptr<const scene::Scene> scene) const
    -> const PublishedRuntimeMotionSnapshot*;
  OXGN_SCNSYNC_API auto UpsertMaterialMotionInput(
    observer_ptr<const scene::Scene> scene,
    const RuntimeMaterialMotionInputState& state) -> void;
  OXGN_SCNSYNC_API auto RemoveMaterialMotionInput(
    observer_ptr<const scene::Scene> scene, const RuntimeMaterialMotionKey& key)
    -> void;

private:
  [[nodiscard]] auto FindMaterialMotionInput(
    observer_ptr<const scene::Scene> scene, const RuntimeMaterialMotionKey& key)
    const -> std::optional<RuntimeMaterialMotionInputState>;
  auto PublishSnapshotForScene(
    const scene::Scene& scene, frame::SequenceNumber frame_sequence) -> void;

  engine::ModulePriority priority_;
  mutable std::shared_mutex published_snapshots_mutex_;
  std::unordered_map<const scene::Scene*, PublishedRuntimeMotionSnapshot>
    published_snapshots_ {};
  mutable std::shared_mutex runtime_material_inputs_mutex_;
  std::unordered_map<const scene::Scene*,
    std::unordered_map<RuntimeMaterialMotionKey, RuntimeMaterialMotionInputState,
      RuntimeMaterialMotionKeyHash>>
    runtime_material_inputs_ {};
};

} // namespace oxygen::scenesync
