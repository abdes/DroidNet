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
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

// We defines and use strong types for bindless slots to avoid accidental
// mixups.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(TypeName)                             \
  struct TypeName {                                                            \
    ShaderVisibleIndex value;                                                  \
    TypeName()                                                                 \
      : TypeName(kInvalidShaderVisibleIndex)                                   \
    {                                                                          \
    }                                                                          \
    explicit constexpr TypeName(const ShaderVisibleIndex v)                    \
      : value(v)                                                               \
    {                                                                          \
    }                                                                          \
    [[nodiscard]] constexpr auto IsValid() const noexcept                      \
    {                                                                          \
      return value != kInvalidShaderVisibleIndex;                              \
    }                                                                          \
    constexpr auto operator<=>(const TypeName&) const = default;               \
  };                                                                           \
  static_assert(sizeof(TypeName) == 4);

OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessDrawMetadataSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessWorldsSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessNormalsSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessMaterialConstantsSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessEnvironmentStaticSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessDirectionalLightsSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessDirectionalShadowsSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessPositionalLightsSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessInstanceDataSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessGpuDebugLineSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BindlessGpuDebugCounterSlot)

#undef OXYGEN_DEFINE_BINDLESS_SLOT_TYPE

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
 Layout mirrors the HLSL cbuffer SceneConstants (b1, space0). It separates
 application-owned fields from renderer-owned fields:

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
 per-item transforms now accessed via stable TransformHandle indirection and
 will be bound/consumed downstream.

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

  struct alignas(packing::kShaderDataFieldAlignment) GpuData {
    frame::SequenceNumber frame_seq_num { 0 };
    frame::Slot::UnderlyingType frame_slot { 0 };
    float time_seconds { 0.0F };

    // Aligned at 16 bytes here
    glm::mat4 view_matrix { 1.0F };
    glm::mat4 projection_matrix { 1.0F };

    // Aligned at 16 bytes here
    glm::vec3 camera_position { 0.0F, 0.0F, 0.0F };
    float exposure { 1.0F };

    // Aligned at 16 bytes here
    BindlessDrawMetadataSlot draw_metadata_bslot;
    BindlessWorldsSlot transforms_bslot;
    BindlessNormalsSlot normal_matrices_bslot;
    BindlessMaterialConstantsSlot material_constants_bslot;

    // Aligned at 16 bytes here
    BindlessEnvironmentStaticSlot env_static_bslot;
    BindlessDirectionalLightsSlot directional_lights_bslot;
    BindlessDirectionalShadowsSlot directional_shadows_bslot;
    BindlessPositionalLightsSlot positional_lights_bslot;

    // Aligned at 16 bytes here
    BindlessInstanceDataSlot instance_data_bslot;
    BindlessGpuDebugLineSlot gpu_debug_line_bslot;
    BindlessGpuDebugCounterSlot gpu_debug_counter_bslot;
    uint32_t _pad_to_16 { 0 }; // padding to 16-byte alignment

    // padding to 256-byte alignment
    glm::vec4 _pad_to_256_1 { 0.0F };
    glm::vec4 _pad_to_256_2 { 0.0F };
    glm::vec4 _pad_to_256_3 { 0.0F };
  };
  // clang-format off
  static_assert(sizeof(GpuData) <= packing::kRootConstantsMaxSize);
  static_assert(offsetof(GpuData, view_matrix) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, projection_matrix) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, camera_position) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, draw_metadata_bslot) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, env_static_bslot) % packing::kShaderDataFieldAlignment == 0);
  static_assert(offsetof(GpuData, instance_data_bslot) % packing::kShaderDataFieldAlignment == 0);
  // clang-format on

  SceneConstants() = default;
  ~SceneConstants() = default;

  OXYGEN_DEFAULT_COPYABLE(SceneConstants)
  OXYGEN_DEFAULT_MOVABLE(SceneConstants)

  // Application setters (chainable) — modern return type
  auto SetViewMatrix(const glm::mat4& m) noexcept -> SceneConstants&;

  OXGN_RNDR_API auto SetProjectionMatrix(const glm::mat4& m) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetCameraPosition(const glm::vec3& p) noexcept
    -> SceneConstants&;

  // Renderer-only setters (require the renderer tag)
  OXGN_RNDR_API auto SetTimeSeconds(float t, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetFrameSlot(frame::Slot slot, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetFrameSequenceNumber(
    frame::SequenceNumber seq, RendererTag) noexcept -> SceneConstants&;

  OXGN_RNDR_API auto SetExposure(float exposure, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessDrawMetadataSlot(
    BindlessDrawMetadataSlot slot, RendererTag) noexcept -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessWorldsSlot(
    BindlessWorldsSlot slot, RendererTag) noexcept -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessNormalMatricesSlot(
    BindlessNormalsSlot slot, RendererTag) noexcept -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessMaterialConstantsSlot(
    BindlessMaterialConstantsSlot slot, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessEnvironmentStaticSlot(
    BindlessEnvironmentStaticSlot slot, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessDirectionalLightsSlot(
    BindlessDirectionalLightsSlot slot, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessDirectionalShadowsSlot(
    BindlessDirectionalShadowsSlot slot, RendererTag) noexcept
    -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessPositionalLightsSlot(
    BindlessPositionalLightsSlot slot, RendererTag) noexcept -> SceneConstants&;

  OXGN_RNDR_API auto SetBindlessInstanceDataSlot(
    BindlessInstanceDataSlot slot, RendererTag) noexcept -> SceneConstants&;
  OXGN_RNDR_API auto SetBindlessGpuDebugLineSlot(
    BindlessGpuDebugLineSlot slot, RendererTag) noexcept -> SceneConstants&;
  OXGN_RNDR_API auto SetBindlessGpuDebugCounterSlot(
    BindlessGpuDebugCounterSlot slot, RendererTag) noexcept -> SceneConstants&;

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

  [[nodiscard]] constexpr auto GetExposure() const noexcept
  {
    return exposure_;
  }

  [[nodiscard]] constexpr auto GetBindlessDrawMetadataSlot() const noexcept
  {
    return draw_metadata_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessWorldsSlot() const noexcept
  {
    return transforms_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessNormalMatricesSlot() const noexcept
  {
    return normal_matrices_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessEnvironmentStaticSlot() const noexcept
  {
    return env_static_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessDirectionalLightsSlot() const noexcept
  {
    return directional_lights_bslot_;
  }

  [[nodiscard]] constexpr auto
  GetBindlessDirectionalShadowsSlot() const noexcept
  {
    return directional_shadows_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessPositionalLightsSlot() const noexcept
  {
    return positional_lights_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessInstanceDataSlot() const noexcept
  {
    return instance_data_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessGpuDebugLineSlot() const noexcept
  {
    return gpu_debug_line_bslot_;
  }

  [[nodiscard]] constexpr auto GetBindlessGpuDebugCounterSlot() const noexcept
  {
    return gpu_debug_counter_bslot_;
  }

  // Monotonic version counter; incremented on any mutation.
  [[nodiscard]] constexpr auto GetVersion() const noexcept { return version_; }

  // Returns a reference to a cached GPU snapshot. Rebuilds only when version_
  // changed.
  OXGN_RNDR_NDAPI auto GetSnapshot() const noexcept -> const GpuData&;

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
      .exposure = exposure_,
      .draw_metadata_bslot = draw_metadata_bslot_,
      .transforms_bslot = transforms_bslot_,
      .normal_matrices_bslot = normal_matrices_bslot_,
      .material_constants_bslot = material_constants_bslot_,
      .env_static_bslot = env_static_bslot_,
      .directional_lights_bslot = directional_lights_bslot_,
      .directional_shadows_bslot = directional_shadows_bslot_,
      .positional_lights_bslot = positional_lights_bslot_,
      .instance_data_bslot = instance_data_bslot_,
      .gpu_debug_line_bslot = gpu_debug_line_bslot_,
      .gpu_debug_counter_bslot = gpu_debug_counter_bslot_,
    };
  }

  // Application-managed fields
  glm::mat4 view_matrix_ { 1.0F };
  glm::mat4 projection_matrix_ { 1.0F };
  glm::vec3 camera_position_ { 0.0F, 0.0F, 0.0F };

  // Renderer-managed fields
  float time_seconds_ { 0.0F };
  frame::Slot frame_slot_;
  frame::SequenceNumber frame_seq_num_;
  float exposure_ { 1.0F };
  BindlessDrawMetadataSlot draw_metadata_bslot_;
  BindlessWorldsSlot transforms_bslot_;
  BindlessNormalsSlot normal_matrices_bslot_;
  BindlessMaterialConstantsSlot material_constants_bslot_;

  BindlessEnvironmentStaticSlot env_static_bslot_;
  BindlessDirectionalLightsSlot directional_lights_bslot_;
  BindlessDirectionalShadowsSlot directional_shadows_bslot_;
  BindlessPositionalLightsSlot positional_lights_bslot_;
  BindlessInstanceDataSlot instance_data_bslot_;
  BindlessGpuDebugLineSlot gpu_debug_line_bslot_;
  BindlessGpuDebugCounterSlot gpu_debug_counter_bslot_;

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

} // namespace oxygen::engine
