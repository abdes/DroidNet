//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {
struct RenderContext;
} // namespace oxygen::engine

namespace oxygen::scene {
class SceneEnvironment;
} // namespace oxygen::scene

namespace oxygen::engine::internal {

class IBrdfLutProvider;
class ISkyAtmosphereLutProvider;

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

 @note This manager does not implement a monotonic versioning mechanism.
       It tracks only content changes (dirty) and per-slot upload needs.
*/
class EnvironmentStaticDataManager {
public:
  OXGN_RNDR_API explicit EnvironmentStaticDataManager(
    observer_ptr<Graphics> gfx,
    observer_ptr<renderer::resources::IResourceBinder> texture_binder,
    observer_ptr<IBrdfLutProvider> brdf_lut_provider,
    observer_ptr<ISkyAtmosphereLutProvider> sky_atmo_lut_provider = nullptr);

  OXGN_RNDR_API ~EnvironmentStaticDataManager();

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentStaticDataManager)
  OXYGEN_DEFAULT_MOVABLE(EnvironmentStaticDataManager)

  //! Set the active frame slot for upcoming uploads.
  OXGN_RNDR_API auto OnFrameStart(frame::Slot slot) -> void;

  //! Rebuild CPU snapshot from the scene environment.
  /*!
   Missing or disabled systems produce deterministic defaults with
   `enabled = 0` in their corresponding GPU-facing structs.

   @param context Render context for the current frame.
  */
  OXGN_RNDR_API auto UpdateIfNeeded(const RenderContext& context) -> void;

  //! Enforce resource state barriers for owned textures (e.g. BRDF LUT).
  /*!
   Call this on the graphics command recorder before rendering to ensure
   textures uploaded on copy queues are correctly transitioned to SRV state.
  */
  OXGN_RNDR_API auto EnforceBarriers(graphics::CommandRecorder& recorder)
    -> void;

  //! Shader-visible SRV index for the environment static data.
  [[nodiscard]] auto GetSrvIndex() const noexcept -> ShaderVisibleIndex
  {
    return srv_index_;
  }

  //! Returns the BRDF LUT texture if available.
  [[nodiscard]] auto GetBrdfLutTexture() const noexcept
    -> std::shared_ptr<graphics::Texture>
  {
    return brdf_lut_texture_;
  }

private:
  static constexpr std::uint32_t kStrideBytes
    = static_cast<std::uint32_t>(sizeof(EnvironmentStaticData));

  observer_ptr<Graphics> gfx_;
  observer_ptr<renderer::resources::IResourceBinder> texture_binder_;
  observer_ptr<IBrdfLutProvider> brdf_lut_provider_;
  observer_ptr<ISkyAtmosphereLutProvider> sky_atmo_lut_provider_;
  frame::Slot current_slot_ { frame::kInvalidSlot };

  EnvironmentStaticData cpu_snapshot_ {};
  std::array<bool, frame::kFramesInFlight.get()> slot_needs_upload_ {};

  std::shared_ptr<graphics::Buffer> buffer_;
  std::shared_ptr<graphics::Texture> brdf_lut_texture_;
  bool brdf_lut_transitioned_ { false };
  void* mapped_ptr_ { nullptr };

  graphics::NativeView srv_view_ {};
  ShaderVisibleIndex srv_index_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex brdf_lut_slot_ { kInvalidShaderVisibleIndex };

  auto BuildFromSceneEnvironment(
    observer_ptr<const scene::SceneEnvironment> env) -> void;
  auto UploadIfNeeded() -> void;

  auto EnsureResourcesCreated() -> void;
  auto MarkAllSlotsDirty() -> void;

  [[nodiscard]] auto CurrentSlotIndex() const noexcept -> std::uint32_t
  {
    return current_slot_.get();
  }
};

} // namespace oxygen::engine::internal
