//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Renderer/Types/DirectionalLightShadows.h>
#include <Oxygen/Renderer/Types/PositionalLightData.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/api_export.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::renderer {

//! Collects scene lights and uploads GPU-facing structured buffers.
/*!
 LightManager is a frame-local collector for scene lights that produces
 GPU-ready arrays:

 - `DirectionalLightBasic[]`
 - `DirectionalLightShadows[]`
 - `PositionalLightData[]` (point + spot)

 The manager uses `engine::upload::TransientStructuredBuffer` to allocate
 per-frame structured buffers and write their contents directly into upload
 memory (no explicit copy commands).

 ### Usage contract

 - Call `OnFrameStart()` once per frame before collecting.
 - Call `CollectFromNode()` during scene traversal (frame-phase), including
   nodes without renderables.
 - Call `EnsureFrameResources()` once collection is complete.
 - Read SRV indices using the `Get*SrvIndex()` accessors, which will lazily
   upload if needed.

 ### Gating

 Extraction applies scene gating rules:
 - Node `kVisible` is a hard gate.
 - `affects_world` must be true.
 - `Baked` mobility lights are excluded.
 - Shadow eligibility requires both `casts_shadows` and node `kCastsShadows`.

 @note This class does not perform per-view culling (Forward+/Clustered).
       That work is staged for later phases.
*/
class LightManager {
public:
  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::InlineTransfersCoordinator;

  OXGN_RNDR_API LightManager(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> provider,
    observer_ptr<CoordinatorT> inline_transfers);

  OXYGEN_MAKE_NON_COPYABLE(LightManager)
  OXYGEN_MAKE_NON_MOVABLE(LightManager)

  OXGN_RNDR_API ~LightManager();

  //! Starts a new frame and resets transient state.
  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;

  //! Clears the collected CPU snapshots.
  OXGN_RNDR_API auto Clear() noexcept -> void;

  //! Collects light data from a scene node (if it contains a light component).
  OXGN_RNDR_API auto CollectFromNode(const scene::SceneNodeImpl& node) -> void;

  //! Ensures transient GPU buffers are allocated and populated for this frame.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Shader-visible SRV index for directional hot data.
  OXGN_RNDR_NDAPI auto GetDirectionalLightsSrvIndex() const
    -> ShaderVisibleIndex;

  //! Shader-visible SRV index for directional shadow payloads.
  OXGN_RNDR_NDAPI auto GetDirectionalShadowsSrvIndex() const
    -> ShaderVisibleIndex;

  //! Shader-visible SRV index for positional (point/spot) light data.
  OXGN_RNDR_NDAPI auto GetPositionalLightsSrvIndex() const
    -> ShaderVisibleIndex;

  //! Read-only access to collected directional light hot data.
  OXGN_RNDR_NDAPI auto GetDirectionalLights() const noexcept
    -> std::span<const engine::DirectionalLightBasic>;

  //! Read-only access to collected directional light shadow data.
  OXGN_RNDR_NDAPI auto GetDirectionalShadows() const noexcept
    -> std::span<const engine::DirectionalLightShadows>;

  //! Read-only access to collected positional light data.
  OXGN_RNDR_NDAPI auto GetPositionalLights() const noexcept
    -> std::span<const engine::PositionalLightData>;

private:
  observer_ptr<Graphics> gfx_;
  observer_ptr<ProviderT> staging_provider_;
  observer_ptr<CoordinatorT> inline_transfers_;

  using BufferT = engine::upload::TransientStructuredBuffer;
  BufferT directional_basic_buffer_;
  BufferT directional_shadows_buffer_;
  BufferT positional_buffer_;

  ShaderVisibleIndex directional_basic_srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex directional_shadows_srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex positional_srv_ { kInvalidShaderVisibleIndex };

  std::vector<engine::DirectionalLightBasic> dir_basic_;
  std::vector<engine::DirectionalLightShadows> dir_shadows_;
  std::vector<engine::PositionalLightData> positional_;

  bool uploaded_this_frame_ { false };
  std::uint64_t frames_started_count_ { 0ULL };
  std::uint64_t nodes_visited_count_ { 0ULL };
  std::uint64_t lights_emitted_count_ { 0ULL };

  std::uint64_t total_nodes_visited_count_ { 0ULL };
  std::uint64_t total_lights_emitted_count_ { 0ULL };
  std::size_t peak_dir_lights_count_ { 0U };
  std::size_t peak_pos_lights_count_ { 0U };

  bool has_completed_frame_snapshot_ { false };
  std::uint64_t last_completed_nodes_visited_count_ { 0ULL };
  std::uint64_t last_completed_lights_emitted_count_ { 0ULL };
  std::size_t last_completed_dir_lights_count_ { 0U };
  std::size_t last_completed_pos_lights_count_ { 0U };
};

} // namespace oxygen::renderer
