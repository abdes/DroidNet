//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <unordered_map>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Types/VelocityPublications.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

struct RenderMotionIdentityKey {
  scene::NodeHandle node_handle {};
  data::AssetKey geometry_asset_key {};
  std::uint32_t lod_index { 0U };
  std::uint32_t submesh_index { 0U };
  VelocityProducerFamily producer_family {
    VelocityProducerFamily::kMaterialWpo
  };
  std::uint64_t contract_hash { 0U };

  [[nodiscard]] constexpr auto operator==(
    const RenderMotionIdentityKey&) const noexcept -> bool
    = default;
};

struct RenderMotionIdentityKeyHash {
  [[nodiscard]] auto operator()(
    const RenderMotionIdentityKey& key) const noexcept -> std::size_t
  {
    std::size_t seed = std::hash<scene::NodeHandle> {}(key.node_handle);
    oxygen::HashCombine(seed, key.geometry_asset_key);
    oxygen::HashCombine(seed, key.lod_index);
    oxygen::HashCombine(seed, key.submesh_index);
    oxygen::HashCombine(seed, static_cast<std::uint32_t>(key.producer_family));
    oxygen::HashCombine(seed, key.contract_hash);
    return seed;
  }
};

template <typename PublicationT> struct PublicationHistorySnapshot {
  PublicationT current {};
  PublicationT previous {};
  bool previous_valid { false };
};

class DeformationHistoryCache {
public:
  OXGN_VRTX_API void BeginFrame(
    std::uint64_t frame_sequence, observer_ptr<const scene::Scene> scene);
  OXGN_VRTX_API auto TouchCurrentMaterialWpo(
    const RenderMotionIdentityKey& key, const MaterialWpoPublication& current)
    -> PublicationHistorySnapshot<MaterialWpoPublication>;
  OXGN_VRTX_API auto TouchCurrentMotionVectorStatus(
    const RenderMotionIdentityKey& key,
    const MotionVectorStatusPublication& current)
    -> PublicationHistorySnapshot<MotionVectorStatusPublication>;
  OXGN_VRTX_API void EndFrame();

private:
  // TODO(vortex/skinned-morph): add renderer-owned current/previous history
  // maps for SkinnedPosePublication and MorphPublication when the engine ships
  // real skinned/morph runtime payloads. The Phase 3 closure scope only
  // requires the current-engine feature envelope; the future extension belongs
  // here rather than in upload helpers.
  template <typename PublicationT> struct Entry {
    PublicationT current {};
    PublicationT previous {};
    std::uint64_t last_seen_frame { 0U };
    bool previous_valid { false };
  };

  template <typename PublicationT>
  auto TouchCurrent(
    std::unordered_map<RenderMotionIdentityKey, Entry<PublicationT>,
      RenderMotionIdentityKeyHash>& entries,
    const RenderMotionIdentityKey& key, const PublicationT& current)
    -> PublicationHistorySnapshot<PublicationT>;

  template <typename PublicationT>
  void EndFrame(std::unordered_map<RenderMotionIdentityKey, Entry<PublicationT>,
    RenderMotionIdentityKeyHash>& entries);

  std::uint64_t current_frame_ { 0U };
  const scene::Scene* current_scene_ { nullptr };
  std::unordered_map<RenderMotionIdentityKey, Entry<MaterialWpoPublication>,
    RenderMotionIdentityKeyHash>
    material_wpo_entries_ {};
  std::unordered_map<RenderMotionIdentityKey,
    Entry<MotionVectorStatusPublication>, RenderMotionIdentityKeyHash>
    motion_vector_status_entries_ {};
};

} // namespace oxygen::vortex::internal
