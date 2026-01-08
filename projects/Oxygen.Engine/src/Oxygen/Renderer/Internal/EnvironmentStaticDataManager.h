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
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {
struct RenderContext;
} // namespace oxygen::engine

namespace oxygen::scene {
class SceneEnvironment;
} // namespace oxygen::scene

namespace oxygen::engine::internal {

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
    observer_ptr<Graphics> gfx);

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

  //! Shader-visible SRV index for the environment static data.
  [[nodiscard]] auto GetSrvIndex() const noexcept -> ShaderVisibleIndex
  {
    return srv_index_;
  }

private:
  static constexpr std::uint32_t kStrideBytes
    = static_cast<std::uint32_t>(sizeof(EnvironmentStaticData));

  observer_ptr<Graphics> gfx_;
  frame::Slot current_slot_ { frame::kInvalidSlot };

  EnvironmentStaticData cpu_snapshot_ {};
  std::array<bool, frame::kFramesInFlight.get()> slot_needs_upload_ {};

  std::shared_ptr<graphics::Buffer> buffer_;
  void* mapped_ptr_ { nullptr };

  graphics::NativeView srv_view_ {};
  ShaderVisibleIndex srv_index_ { kInvalidShaderVisibleIndex };

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
