//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::engine {

//! Sentinel value for an invalid or unassigned shader-visible descriptor slot.
/*!
 Used to indicate that a structured buffer SRV (e.g., DrawResourceIndices)
 is not available this frame. Shaders must check for this value and branch
 accordingly instead of assuming a valid slot.
*/
inline constexpr uint32_t kInvalidDescriptorSlot
  = (std::numeric_limits<uint32_t>::max)();

struct BindlessDrawMetadataSlot {
  uint32_t value;
  explicit constexpr BindlessDrawMetadataSlot(
    const uint32_t v = kInvalidDescriptorSlot)
    : value(v)
  {
  }
  constexpr auto operator<=>(const BindlessDrawMetadataSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value; }
};

struct BindlessWorldsSlot {
  uint32_t value;
  explicit constexpr BindlessWorldsSlot(
    const uint32_t v = kInvalidDescriptorSlot)
    : value(v)
  {
  }
  constexpr auto operator<=>(const BindlessWorldsSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value; }
};

struct BindlessMaterialConstantsSlot {
  uint32_t value;
  explicit constexpr BindlessMaterialConstantsSlot(
    const uint32_t v = kInvalidDescriptorSlot)
    : value(v)
  {
  }
  constexpr auto operator<=>(const BindlessMaterialConstantsSlot&) const
    = default;
  constexpr operator uint32_t() const noexcept { return value; }
};

struct MonotonicVersion {
  uint64_t value { 0 };
  explicit constexpr MonotonicVersion(const uint64_t v = 0)
    : value(v)
  {
  }
  constexpr auto operator<=>(const MonotonicVersion&) const = default;
  constexpr operator uint64_t() const noexcept { return value; }

  //! Only Next() is provided; mutation is done via the owner using Next().
  [[nodiscard]] constexpr auto Next() const noexcept -> MonotonicVersion
  {
    return MonotonicVersion(value + 1);
  }
};

//! CPU-side manager for per-frame scene (view) constants.
/*!
 This class is a CPU-side manager whose layout mirrors the HLSL cbuffer
 SceneConstants (b1, space0). It separates application-owned fields from
 renderer-owned fields:

  - Application responsibilities: set view/projection matrices and camera
    position via the application-facing setters (SetViewMatrix,
    SetProjectionMatrix, SetCameraPosition).

  - Renderer responsibilities: set time, frame index and shader-visible
    descriptor slots via the renderer-only setters that require the explicit
    RendererTag. The tag is intentionally explicit to make renderer ownership
    clear at call-sites.

 The object is versioned: any setter bumps a monotonic version counter. To
 produce a GPU upload payload call GetSnapshot(); it returns a const reference
 to a per-instance cached GpuData which is rebuilt lazily when the internal
 version differs from the cached_version. This avoids unnecessary CPU->GPU
 uploads when nothing changed.

 Multiple mutations per frame are allowed; the implementation is "last-wins" for
 values. Note that world/object transforms are intentionally NOT included here:
 per-item transforms remain part of per-draw data (RenderItem.world_transform)
 and will be bound/consumed downstream.

 Fields contained in the GPU snapshot include:
   - view_matrix / projection_matrix: Camera basis.
   - camera_position: World-space camera origin.
   - time_seconds: Accumulated time (seconds) for temporal effects.
   - frame_index: Monotonic frame counter wrapped in a strong type (FrameIndex).
   - bindless_draw_metadata_slot: Shader-visible descriptor slot for
 DrawMetadata structured buffer (BindlessDrawMetadataSlot); use
 kInvalidDescriptorSlot when unavailable.

 Alignment: Each glm::mat4 occupies 64 bytes (column-major). frame_index is a
 32-bit value that begins a 16-byte register; the remaining 12 bytes of that
 register are reserved so the total struct size remains a multiple of 16 bytes
 (root CBV requirement on D3D12).
*/
class SceneConstants final {
public:
  struct RendererTag {
    explicit constexpr RendererTag() = default;
  };
  static constexpr RendererTag kRenderer {};

  struct GpuData {
    glm::mat4 view_matrix { 1.0f };
    glm::mat4 projection_matrix { 1.0f };
    glm::vec3 camera_position { 0.0f, 0.0f, 0.0f };
    float time_seconds { 0.0f };
    frame::Slot::UnderlyingType frame_slot { 0 };
    frame::SequenceNumber::UnderlyingType frame_seq_num { 0 };
    uint32_t bindless_draw_metadata_slot { kInvalidDescriptorSlot };
    uint32_t bindless_transforms_slot { kInvalidDescriptorSlot };
    uint32_t bindless_material_constants_slot { kInvalidDescriptorSlot };
    uint32_t _pad0 { 0 };
    uint32_t _pad1 { 0 };
    uint32_t _pad2 { 0 };
    uint32_t _pad3 { 0 };
  };
  static_assert(
    sizeof(GpuData) % 16 == 0, "GpuData size must be 16-byte aligned");

  SceneConstants() = default;
  OXYGEN_DEFAULT_COPYABLE(SceneConstants)
  OXYGEN_DEFAULT_MOVABLE(SceneConstants)
  ~SceneConstants() = default;

  // Application setters (chainable) — modern return type
  auto SetViewMatrix(const glm::mat4& m) noexcept -> SceneConstants&
  {
    if (std::memcmp(&view_matrix_, &m, sizeof(glm::mat4)) != 0) {
      view_matrix_ = m;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetProjectionMatrix(const glm::mat4& m) noexcept -> SceneConstants&
  {
    if (std::memcmp(&projection_matrix_, &m, sizeof(glm::mat4)) != 0) {
      projection_matrix_ = m;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetCameraPosition(const glm::vec3& p) noexcept -> SceneConstants&
  {
    if (camera_position_.x != p.x || camera_position_.y != p.y
      || camera_position_.z != p.z) {
      camera_position_ = p;
      version_ = version_.Next();
    }
    return *this;
  }

  // Renderer-only setters (require the renderer tag)
  auto SetTimeSeconds(const float t, RendererTag) noexcept -> SceneConstants&
  {
    if (time_seconds_ != t) {
      time_seconds_ = t;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetFrameSlot(const frame::Slot slot, RendererTag) noexcept
    -> SceneConstants&
  {
    if (frame_slot_ != slot) {
      frame_slot_ = slot;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetFrameSequenceNumber(
    const frame::SequenceNumber seq, RendererTag) noexcept -> SceneConstants&
  {
    if (frame_seq_num_ != seq) {
      frame_seq_num_ = seq;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetBindlessDrawMetadataSlot(const BindlessDrawMetadataSlot slot,
    RendererTag) noexcept -> SceneConstants&
  {
    if (bindless_draw_metadata_slot_ != slot) {
      bindless_draw_metadata_slot_ = slot;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetBindlessWorldsSlot(
    const BindlessWorldsSlot slot, RendererTag) noexcept -> SceneConstants&
  {
    if (bindless_transforms_slot_ != slot) {
      bindless_transforms_slot_ = slot;
      version_ = version_.Next();
    }
    return *this;
  }

  auto SetBindlessMaterialConstantsSlot(
    const BindlessMaterialConstantsSlot slot, RendererTag) noexcept
    -> SceneConstants&
  {
    if (bindless_material_constants_slot_ != slot) {
      bindless_material_constants_slot_ = slot;
      version_ = version_.Next();
    }
    return *this;
  }

  // Getters use GetXXX to avoid conflicts with strong types
  [[nodiscard]] auto GetViewMatrix() const noexcept { return view_matrix_; }
  [[nodiscard]] auto GetProjectionMatrix() const noexcept
  {
    return projection_matrix_;
  }
  [[nodiscard]] auto GetCameraPosition() const noexcept
  {
    return camera_position_;
  }
  [[nodiscard]] constexpr auto GetTimeSeconds() const noexcept
  {
    return time_seconds_;
  }
  [[nodiscard]] constexpr auto GetFrameIndex() const noexcept
  {
    return frame_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessDrawMetadataSlot() const noexcept
  {
    return bindless_draw_metadata_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessWorldsSlot() const noexcept
  {
    return bindless_transforms_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessMaterialConstantsSlot() const noexcept
  {
    return bindless_material_constants_slot_;
  }

  // Monotonic version counter; incremented on any mutation.
  [[nodiscard]] constexpr auto GetVersion() const noexcept { return version_; }

  // Returns a reference to a cached GPU snapshot. Rebuilds only when version_
  // changed.
  [[nodiscard]] auto GetSnapshot() const noexcept
  {
    if (cached_version_ != version_) {
      RebuildCache();
      cached_version_ = version_;
    }
    return cached_;
  }

private:
  auto RebuildCache() const noexcept -> void
  {
    cached_ = GpuData {
      .view_matrix = view_matrix_,
      .projection_matrix = projection_matrix_,
      .camera_position = camera_position_,
      .time_seconds = time_seconds_,
      .frame_slot = frame_slot_.get(),
      .frame_seq_num = frame_seq_num_.get(),
      .bindless_draw_metadata_slot = bindless_draw_metadata_slot_.value,
      .bindless_transforms_slot = bindless_transforms_slot_.value,
      .bindless_material_constants_slot
      = bindless_material_constants_slot_.value,
    };
  }

  // Application-managed fields
  glm::mat4 view_matrix_ { 1.0f };
  glm::mat4 projection_matrix_ { 1.0f };
  glm::vec3 camera_position_ { 0.0f, 0.0f, 0.0f };

  // Renderer-managed fields
  float time_seconds_ { 0.0f };
  frame::Slot frame_slot_ {};
  frame::SequenceNumber frame_seq_num_ {};
  BindlessDrawMetadataSlot bindless_draw_metadata_slot_ {};
  BindlessWorldsSlot bindless_transforms_slot_ {};
  BindlessMaterialConstantsSlot bindless_material_constants_slot_ {};

  // Versioning + cache
  MonotonicVersion version_ { 0 };
  mutable MonotonicVersion cached_version_ { (
    std::numeric_limits<uint64_t>::max)() };
  mutable GpuData cached_ {};
};

} // namespace oxygen::engine
