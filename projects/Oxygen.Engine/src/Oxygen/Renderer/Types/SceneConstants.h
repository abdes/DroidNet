//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>

namespace oxygen::engine {

//! Sentinel value for an invalid or unassigned shader-visible descriptor slot.
/*!
 Used to indicate that a structured buffer SRV (e.g., DrawResourceIndices)
 is not available this frame. Shaders must check for this value and branch
 accordingly instead of assuming a valid slot.
*/
inline constexpr uint32_t kInvalidDescriptorSlot
  = (std::numeric_limits<uint32_t>::max)();

struct FrameIndex {
  uint32_t value;
  explicit constexpr FrameIndex(const uint32_t v = 0)
    : value(v)
  {
  }
  constexpr auto operator<=>(const FrameIndex&) const = default;
  constexpr operator uint32_t() const noexcept { return value; }
};

struct BindlessIndicesSlot {
  uint32_t value;
  explicit constexpr BindlessIndicesSlot(
    const uint32_t v = kInvalidDescriptorSlot)
    : value(v)
  {
  }
  constexpr auto operator<=>(const BindlessIndicesSlot&) const = default;
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
   - bindless_indices_slot: Shader-visible descriptor slot wrapped in a strong
     type (BindlessIndicesSlot); use kInvalidDescriptorSlot when unavailable.

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
    uint32_t frame_index { 0 };
    uint32_t bindless_indices_slot { kInvalidDescriptorSlot };
    uint32_t reserved[2] { 0, 0 };
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
    view_matrix_ = m;
    version_ = version_.Next();
    return *this;
  }

  auto SetProjectionMatrix(const glm::mat4& m) noexcept -> SceneConstants&
  {
    projection_matrix_ = m;
    version_ = version_.Next();
    return *this;
  }

  auto SetCameraPosition(const glm::vec3& p) noexcept -> SceneConstants&
  {
    camera_position_ = p;
    version_ = version_.Next();
    return *this;
  }

  // Renderer-only setters (require the renderer tag)
  auto SetTimeSeconds(const float t, RendererTag) noexcept -> SceneConstants&
  {
    time_seconds_ = t;
    version_ = version_.Next();
    return *this;
  }

  auto SetFrameIndex(const FrameIndex idx, RendererTag) noexcept
    -> SceneConstants&
  {
    frame_index_ = idx;
    version_ = version_.Next();
    return *this;
  }

  auto SetBindlessIndicesSlot(
    const BindlessIndicesSlot slot, RendererTag) noexcept -> SceneConstants&
  {
    bindless_indices_slot_ = slot;
    version_ = version_.Next();
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
    return frame_index_;
  }

  //! Shader-visible descriptor heap slot of DrawResourceIndices structured
  //! buffer SRV (dynamic; kInvalidDescriptorSlot when unavailable). Shaders
  //! must read and branch rather than assuming slot 0.
  [[nodiscard]] constexpr auto GetBindlessIndicesSlot() const noexcept
  {
    return bindless_indices_slot_;
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
      .frame_index = frame_index_.value,
      .bindless_indices_slot = bindless_indices_slot_.value,
      .reserved = { 0u, 0u },
    };
  }

  // Application-managed fields
  glm::mat4 view_matrix_ { 1.0f };
  glm::mat4 projection_matrix_ { 1.0f };
  glm::vec3 camera_position_ { 0.0f, 0.0f, 0.0f };

  // Renderer-managed fields
  float time_seconds_ { 0.0f };
  FrameIndex frame_index_ {};
  BindlessIndicesSlot bindless_indices_slot_ {};

  // Versioning + cache
  MonotonicVersion version_ { 0 };
  mutable MonotonicVersion cached_version_ { (
    std::numeric_limits<uint64_t>::max)() };
  mutable GpuData cached_ {};
};

} // namespace oxygen::engine
