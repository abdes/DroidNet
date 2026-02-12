//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Internal/ISkyCaptureProvider.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Texture;
  class CommandRecorder;
} // namespace graphics
} // namespace oxygen::graphics

namespace oxygen::engine {
struct RenderContext;
} // namespace oxygen::engine

namespace oxygen::scene {
class SceneEnvironment;
} // namespace oxygen::scene

namespace oxygen::engine::internal {

class IBrdfLutProvider;
class IIblProvider;
class ISkyAtmosphereLutProvider;
class ISkyCaptureProvider;

//! Single-owner builder/uploader for bindless EnvironmentStaticData.
/*!
 Maintains a canonical CPU-side EnvironmentStaticData snapshot derived from the
 scene-authored SceneEnvironment and provides a stable shader-visible SRV slot
 to that data.

 ### Frames In Flight
 The underlying GPU buffer contains one element per frame slot
 (frame::kFramesInFlight). Shaders index into the buffer using
 SceneConstants.frame_slot.

 To stay safe with multiple frames in flight, the manager only writes the
 element corresponding to the *current* frame slot. When the snapshot changes,
 the manager marks all slots as needing upload; each slot is refreshed the next
 time it becomes current.

 ### Usage & Threading
 This class is single-owner and not thread-safe: all public methods must be
 called from the renderer thread (or otherwise externally synchronized).
 Call `OnFrameStart(renderer::RendererTag tag, frame::Slot slot)` at the start
 of each frame (before `UpdateIfNeeded`) to set the active frame slot used for
 uploads. `UpdateIfNeeded(renderer::RendererTag tag, const RenderContext&)`
 rebuilds the CPU snapshot from the provided `RenderContext` and will schedule
 an upload for the current slot when necessary.

 The manager integrates with BRDF LUT, IBL and sky-atmosphere providers and
 with a bindless texture binder to publish shader-visible descriptor slots.
*/
class EnvironmentStaticDataManager {
public:
  OXGN_RNDR_API explicit EnvironmentStaticDataManager(
    observer_ptr<Graphics> gfx,
    observer_ptr<renderer::resources::IResourceBinder> texture_binder,
    observer_ptr<IBrdfLutProvider> brdf_lut_provider,
    observer_ptr<IIblProvider> ibl_manager,
    observer_ptr<ISkyCaptureProvider> sky_capture_provider = nullptr);

  OXGN_RNDR_API ~EnvironmentStaticDataManager();

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentStaticDataManager)
  OXYGEN_DEFAULT_MOVABLE(EnvironmentStaticDataManager)

  //! Set the active frame slot for upcoming uploads.
  OXGN_RNDR_API auto OnFrameStart(renderer::RendererTag tag, frame::Slot slot)
    -> void;

  //! Rebuild CPU snapshot from the scene environment.
  /*!
   Missing or disabled systems produce deterministic defaults with
   `enabled = 0` in their corresponding GPU-facing structs.

   @param context Render context for the current frame.
  */
  OXGN_RNDR_API auto UpdateIfNeeded(renderer::RendererTag tag,
    const RenderContext& context, ViewId view_id) -> void;

  //! Enforce resource state barriers for owned textures (e.g. BRDF LUT).
  /*!
   Call this on the graphics command recorder before rendering to ensure
   textures uploaded on copy queues are correctly transitioned to SRV state.
  */
  OXGN_RNDR_API auto EnforceBarriers(graphics::CommandRecorder& recorder)
    -> void;

  //! Request an IBL regeneration on the next frame.
  OXGN_RNDR_API auto RequestIblRegeneration(ViewId view_id) noexcept -> void;

  //! Returns true if an IBL regeneration has been requested.
  [[nodiscard]] auto IsIblRegenerationRequested(ViewId view_id) const noexcept
    -> bool;

  //! Clears the IBL regeneration request flag.
  auto MarkIblRegenerationClean(ViewId view_id) noexcept -> void;

  //! Shader-visible SRV index for the environment static data.
  [[nodiscard]] auto GetSrvIndex(ViewId view_id) const noexcept
    -> ShaderVisibleIndex;

  //! Returns the BRDF LUT texture if available.
  [[nodiscard]] auto GetBrdfLutTexture() const noexcept
    -> std::shared_ptr<graphics::Texture>
  {
    return brdf_lut_texture_;
  }

  //! Returns the current shader-visible slot for the BRDF LUT.
  /*!
   When the LUT is not ready, this returns kInvalidShaderVisibleIndex.
  */
  [[nodiscard]] auto GetBrdfLutSlot() const noexcept -> ShaderVisibleIndex
  {
    return brdf_lut_slot_;
  }

  //! Returns the current SkyLight cubemap slot.
  [[nodiscard]] auto GetSkyLightCubemapSlot(ViewId view_id) const noexcept
    -> ShaderVisibleIndex;
  //! Returns the current SkyLight cubemap slot for the active view.
  [[nodiscard]] auto GetSkyLightCubemapSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns the sky light IBL radiance scale.
  [[nodiscard]] auto GetSkyLightRadianceScale() const noexcept -> float
  {
    return cpu_snapshot_.sky_light.enabled != 0U
      ? cpu_snapshot_.sky_light.radiance_scale
      : 1.0F;
  }

  [[nodiscard]] auto IsSkyLightCapturedSceneSource(ViewId view_id) const noexcept
    -> bool;
  [[nodiscard]] auto IsSkyLightCapturedSceneSource() const noexcept -> bool
  ;

  //! Returns the current SkySphere cubemap slot.
  [[nodiscard]] auto GetSkySphereCubemapSlot(ViewId view_id) const noexcept
    -> ShaderVisibleIndex;
  //! Returns the current SkySphere cubemap slot for the active view.
  [[nodiscard]] auto GetSkySphereCubemapSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns the sky sphere intensity multiplier.
  [[nodiscard]] auto GetSkySphereIntensity() const noexcept -> float
  {
    return cpu_snapshot_.sky_sphere.enabled != 0U
      ? cpu_snapshot_.sky_sphere.intensity
      : 1.0F;
  }

  //! Returns current sky-capture generation for a view.
  [[nodiscard]] auto GetSkyCaptureGeneration(ViewId view_id) const noexcept
    -> std::uint64_t
  {
    return sky_capture_provider_
      ? sky_capture_provider_->GetCaptureGeneration(view_id)
      : 0ULL;
  }

  auto EraseViewState(ViewId view_id) -> void;

private:
  static constexpr std::uint32_t kStrideBytes
    = static_cast<std::uint32_t>(sizeof(EnvironmentStaticData));

  observer_ptr<Graphics> gfx_;
  observer_ptr<renderer::resources::IResourceBinder> texture_binder_;
  observer_ptr<IBrdfLutProvider> brdf_lut_provider_;
  observer_ptr<IIblProvider> ibl_provider_;
  observer_ptr<ISkyCaptureProvider> sky_capture_provider_;
  frame::Slot current_slot_ { frame::kInvalidSlot };
  ViewId active_view_id_ {};

  // Last frame identity observed by UpdateIfNeeded(). Used only for logging to
  // correlate uploads and publication with the renderer's frame lifecycle.
  frame::Slot last_update_frame_slot_ { frame::kInvalidSlot };
  frame::SequenceNumber last_update_frame_sequence_ { 0 };

  struct ViewState {
    EnvironmentStaticData cpu_snapshot {};
    EnvironmentStaticData published_snapshot {};
    bool has_published_snapshot { false };
    std::uint64_t snapshot_id { 1 };
    std::array<std::uint64_t, frame::kFramesInFlight.get()> slot_uploaded_id {};
    std::uint64_t last_capture_generation { 0 };
    std::uint64_t last_published_atmo_content_version { 0 };
    std::uint64_t last_warned_capture_missing_source_generation { 0 };
    std::uint64_t last_warned_capture_outputs_not_ready_generation { 0 };
    std::uint64_t last_warned_capture_stale_ibl_generation { 0 };
    std::uint64_t last_observed_ibl_source_content_version { 0 };
    EnvironmentStaticData last_coherent_snapshot {};
    bool has_last_coherent_snapshot { false };
    uint32_t incoherent_frame_count { 0 };
    frame::SequenceNumber last_incoherent_logged_sequence { 0 };
    bool ibl_matches_capture_content { true };
    bool use_last_coherent_fallback { false };
    bool coherence_threshold_crossed { false };
    bool ibl_regeneration_requested { false };
    std::shared_ptr<graphics::Buffer> buffer;
    void* mapped_ptr { nullptr };
    graphics::NativeView srv_view;
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
  };
  std::unordered_map<ViewId, ViewState> view_states_;

  EnvironmentStaticData cpu_snapshot_ {};
  EnvironmentStaticData published_snapshot_ {};
  bool has_published_snapshot_ { false };
  // Monotonic snapshot id and per-slot uploaded snapshot ids.
  // When the CPU snapshot changes, increment `snapshot_id_` so every slot
  // becomes implicitly dirty. Each slot records the snapshot id it last
  // uploaded; when it differs from `snapshot_id_` the slot needs upload.
  std::uint64_t snapshot_id_ { 1 };
  std::array<std::uint64_t, frame::kFramesInFlight.get()> slot_uploaded_id_ {};

  std::uint64_t last_capture_generation_ { 0 };
  std::uint64_t last_published_atmo_content_version_ { 0 };
  std::uint64_t last_warned_capture_missing_source_generation_ { 0 };
  std::uint64_t last_warned_capture_outputs_not_ready_generation_ { 0 };
  std::uint64_t last_warned_capture_stale_ibl_generation_ { 0 };
  std::uint64_t last_observed_ibl_source_content_version_ { 0 };
  EnvironmentStaticData last_coherent_snapshot_ {};
  bool has_last_coherent_snapshot_ { false };
  uint32_t incoherent_frame_count_ { 0 };
  frame::SequenceNumber last_incoherent_logged_sequence_ { 0 };
  bool ibl_matches_capture_content_ { true };
  bool use_last_coherent_fallback_ { false };
  bool coherence_threshold_crossed_ { false };
  bool current_snapshot_coherent_ { true };
  bool ibl_regeneration_requested_ { false };

  std::shared_ptr<graphics::Buffer> buffer_;
  std::shared_ptr<graphics::Texture> brdf_lut_texture_;
  bool brdf_lut_transitioned_ { false };
  void* mapped_ptr_ { nullptr };

  graphics::NativeView srv_view_;
  ShaderVisibleIndex srv_index_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex brdf_lut_slot_ { kInvalidShaderVisibleIndex };

  // Build helpers split out of the original monolithic method.
  auto BuildFromSceneEnvironment(
    observer_ptr<const scene::SceneEnvironment> env,
    observer_ptr<ISkyAtmosphereLutProvider> sky_lut_provider) -> void;

  auto ProcessBrdfLut() -> void;
  auto PopulateFog(observer_ptr<const scene::SceneEnvironment> env,
    EnvironmentStaticData& next) -> void;
  auto PopulateAtmosphere(observer_ptr<const scene::SceneEnvironment> env,
    EnvironmentStaticData& next,
    observer_ptr<ISkyAtmosphereLutProvider> sky_lut_provider) -> void;
  auto PopulateSkyLight(observer_ptr<const scene::SceneEnvironment> env,
    EnvironmentStaticData& next) -> void;
  auto PopulateSkySphere(observer_ptr<const scene::SceneEnvironment> env,
    EnvironmentStaticData& next) -> void;
  auto PopulateSkyCapture(EnvironmentStaticData& next) -> void;
  auto PopulateIbl(EnvironmentStaticData& next) -> void;
  auto PopulateClouds(observer_ptr<const scene::SceneEnvironment> env,
    EnvironmentStaticData& next) -> void;
  auto PopulatePostProcess(observer_ptr<const scene::SceneEnvironment> env,
    EnvironmentStaticData& next) -> void;
  auto UploadIfNeeded() -> void;
  auto RefreshCoherentSnapshotState() -> void;

  auto EnsureResourcesCreated() -> void;
  auto MarkAllSlotsDirty() -> void;
  auto LoadViewState(ViewId view_id) -> void;
  auto StoreViewState(ViewId view_id) -> void;
  auto GetOrCreateViewState(ViewId view_id) -> ViewState&;

  [[nodiscard]] auto CurrentSlotIndex() const noexcept -> std::uint32_t
  {
    return current_slot_.get();
  }
};

} // namespace oxygen::engine::internal
