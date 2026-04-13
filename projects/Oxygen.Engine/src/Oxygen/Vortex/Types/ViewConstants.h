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

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

// Strong slot wrapper for the top-level per-view routing payload.
struct BindlessViewFrameBindingsSlot {
  ShaderVisibleIndex value;
  BindlessViewFrameBindingsSlot()
    : BindlessViewFrameBindingsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessViewFrameBindingsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessViewFrameBindingsSlot&) const
    = default;
};
static_assert(sizeof(BindlessViewFrameBindingsSlot) == 4);

struct MonotonicVersion {
  uint64_t value { 0 };
  MonotonicVersion()
    : MonotonicVersion(0)
  {
  }
  explicit constexpr MonotonicVersion(const uint64_t v)
    : value(v)
  {
  }
  constexpr auto operator<=>(const MonotonicVersion&) const = default;

  //! Only Next() is provided; mutation is done via the owner using Next().
  [[nodiscard]] constexpr auto Next() const noexcept -> MonotonicVersion
  {
    return MonotonicVersion(value + 1);
  }
};

//! CPU-side for per-frame scene (view) constants.
/*!
 Layout mirrors the HLSL cbuffer ViewConstants (b1, space0). It separates
 application-owned fields from renderer-owned fields:

  - Application responsibilities: set view/projection matrices and camera
    position via the application-facing setters (SetViewMatrix,
    SetProjectionMatrix, SetCameraPosition).

  - Renderer responsibilities: set time, frame index, and the top-level

 shader-visible view-routing slot via the renderer-only setters that
    require
 the explicit RendererTag. The tag is intentionally explicit to
    make
 renderer ownership clear at call-sites.
 The object is versioned: any setter
 bumps a monotonic version counter. To produce a GPU upload payload call
 GetSnapshot(); it returns a const reference to a per-instance cached GpuData
 which is rebuilt lazily when the internal version differs from the
 cached_version. This avoids unnecessary CPU->GPU uploads when nothing changed.

 Multiple mutations per frame are allowed; the implementation is "last-wins" for
 values. Note that world/object transforms are intentionally NOT included here:
 per-item transforms now accessed via stable TransformHandle indirection and
 will be bound/consumed downstream.

 Alignment: Each glm::mat4 occupies 64 bytes (column-major). The top-level

 routing slot begins a 16-byte register; the remaining 12 bytes of that

 register are reserved so the total struct size remains a multiple of 16 bytes

 (root CBV requirement on D3D12).
*/
class ViewConstants final {
public:
  struct RendererTag {
    explicit constexpr RendererTag() = default;
  };
  static constexpr RendererTag kRenderer {};

  struct alignas(packing::kShaderDataFieldAlignment) GpuData {
    frame::SequenceNumber frame_seq_num { 0 };
    frame::Slot::UnderlyingType frame_slot { 0 };
    float time_seconds { 0.0F };

    // Aligned at 16 bytes here
    glm::mat4 view_matrix { 1.0F };
    glm::mat4 projection_matrix { 1.0F };

    // Aligned at 16 bytes here
    glm::vec3 camera_position { 0.0F, 0.0F, 0.0F };
    float _pad0 { 0.0F };

    // Aligned at 16 bytes here
    BindlessViewFrameBindingsSlot view_frame_bindings_bslot;
    std::uint32_t _pad1 { 0 };
    std::uint32_t _pad2 { 0 };
    std::uint32_t _pad3 { 0 };

    // padding to 256-byte alignment
    glm::vec4 _pad_to_256_1 { 0.0F };
    glm::vec4 _pad_to_256_2 { 0.0F };
    glm::vec4 _pad_to_256_3 { 0.0F };
    glm::vec4 _pad_to_256_4 { 0.0F };
    glm::vec4 _pad_to_256_5 { 0.0F };
  };
  // clang-format off
  static_assert(sizeof(GpuData) <= packing::kRootConstantsMaxSize);
  static_assert(offsetof(GpuData, view_matrix) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, projection_matrix) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, camera_position) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, view_frame_bindings_bslot) % packing::kShaderDataFieldAlignment == 0);
  // clang-format on

  ViewConstants() = default;
  ~ViewConstants() = default;

  OXYGEN_DEFAULT_COPYABLE(ViewConstants)
  OXYGEN_DEFAULT_MOVABLE(ViewConstants)

  // Application setters (chainable) — modern return type
  OXGN_VRTX_API auto SetViewMatrix(const glm::mat4& m) noexcept
    -> ViewConstants&;

  OXGN_VRTX_API auto SetProjectionMatrix(const glm::mat4& m) noexcept
    -> ViewConstants&;
  OXGN_VRTX_API auto SetStableProjectionMatrix(const glm::mat4& m) noexcept
    -> ViewConstants&;

  OXGN_VRTX_API auto SetCameraPosition(const glm::vec3& p) noexcept
    -> ViewConstants&;

  // Renderer-only setters (require the renderer tag)
  OXGN_VRTX_API auto SetTimeSeconds(float t, RendererTag) noexcept
    -> ViewConstants&;

  OXGN_VRTX_API auto SetFrameSlot(frame::Slot slot, RendererTag) noexcept
    -> ViewConstants&;

  OXGN_VRTX_API auto SetFrameSequenceNumber(
    frame::SequenceNumber seq, RendererTag) noexcept -> ViewConstants&;

  OXGN_VRTX_API auto SetBindlessViewFrameBindingsSlot(
    BindlessViewFrameBindingsSlot slot, RendererTag) noexcept -> ViewConstants&;

  // Getters use GetXXX to avoid conflicts with strong types
  [[nodiscard]] auto GetViewMatrix() const noexcept { return view_matrix_; }
  [[nodiscard]] auto GetProjectionMatrix() const noexcept
  {
    return projection_matrix_;
  }
  [[nodiscard]] auto GetStableProjectionMatrix() const noexcept
  {
    return stable_projection_matrix_;
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

  [[nodiscard]] constexpr auto GetBindlessViewFrameBindingsSlot() const noexcept
  {
    return view_frame_bindings_bslot_;
  }

  // Monotonic version counter; incremented on any mutation.
  [[nodiscard]] constexpr auto GetVersion() const noexcept { return version_; }

  // Returns a reference to a cached GPU snapshot. Rebuilds only when version_
  // changed.
  OXGN_VRTX_NDAPI auto GetSnapshot() const noexcept -> const GpuData&;

private:
  auto RebuildCache() const noexcept -> void
  {
    cached_ = GpuData {
      .frame_seq_num = frame_seq_num_,
      .frame_slot = frame_slot_.get(),
      .time_seconds = time_seconds_,
      .view_matrix = view_matrix_,
      .projection_matrix = projection_matrix_,
      .camera_position = camera_position_,
      .view_frame_bindings_bslot = view_frame_bindings_bslot_,
    };
  }

  // Application-managed fields
  glm::mat4 view_matrix_ { 1.0F };
  glm::mat4 projection_matrix_ { 1.0F };
  glm::mat4 stable_projection_matrix_ { 1.0F };
  bool stable_projection_explicit_ { false };
  glm::vec3 camera_position_ { 0.0F, 0.0F, 0.0F };

  // Renderer-managed fields
  float time_seconds_ { 0.0F };
  frame::Slot frame_slot_;
  frame::SequenceNumber frame_seq_num_;
  BindlessViewFrameBindingsSlot view_frame_bindings_bslot_;

  // Versioning + cache
  MonotonicVersion version_ { 0 };
  mutable MonotonicVersion cached_version_ { (
    std::numeric_limits<uint64_t>::max)() };
  OXYGEN_DIAGNOSTIC_PUSH
  OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4324)
  OXYGEN_DIAGNOSTIC_DISABLE_CLANG("-Wpadded")
  mutable GpuData cached_ {};
  OXYGEN_DIAGNOSTIC_POP
};

} // namespace oxygen::vortex
