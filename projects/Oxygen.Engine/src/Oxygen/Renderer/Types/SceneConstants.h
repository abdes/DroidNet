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
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

struct BindlessDrawMetadataSlot {
  ShaderVisibleIndex value;
  BindlessDrawMetadataSlot()
    : BindlessDrawMetadataSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessDrawMetadataSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessDrawMetadataSlot&) const = default;
};

struct BindlessWorldsSlot {
  ShaderVisibleIndex value;
  BindlessWorldsSlot()
    : BindlessWorldsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessWorldsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessWorldsSlot&) const = default;
};

struct BindlessNormalsSlot {
  ShaderVisibleIndex value;
  BindlessNormalsSlot()
    : BindlessNormalsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessNormalsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessNormalsSlot&) const = default;
};

struct BindlessMaterialConstantsSlot {
  ShaderVisibleIndex value;
  BindlessMaterialConstantsSlot()
    : BindlessMaterialConstantsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessMaterialConstantsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessMaterialConstantsSlot&) const
    = default;
};

struct BindlessEnvironmentStaticSlot {
  ShaderVisibleIndex value;
  BindlessEnvironmentStaticSlot()
    : BindlessEnvironmentStaticSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessEnvironmentStaticSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessEnvironmentStaticSlot&) const
    = default;
};

struct BindlessDirectionalLightsSlot {
  ShaderVisibleIndex value;
  BindlessDirectionalLightsSlot()
    : BindlessDirectionalLightsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessDirectionalLightsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessDirectionalLightsSlot&) const
    = default;
};

struct BindlessDirectionalShadowsSlot {
  ShaderVisibleIndex value;
  BindlessDirectionalShadowsSlot()
    : BindlessDirectionalShadowsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessDirectionalShadowsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessDirectionalShadowsSlot&) const
    = default;
};

struct BindlessPositionalLightsSlot {
  ShaderVisibleIndex value;
  BindlessPositionalLightsSlot()
    : BindlessPositionalLightsSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessPositionalLightsSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessPositionalLightsSlot&) const
    = default;
};

// Bindless slot for per-instance data buffer (GPU instancing).
struct BindlessInstanceDataSlot {
  ShaderVisibleIndex value;
  BindlessInstanceDataSlot()
    : BindlessInstanceDataSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessInstanceDataSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessInstanceDataSlot&) const = default;
};

struct BindlessGpuDebugLineSlot {
  ShaderVisibleIndex value;
  BindlessGpuDebugLineSlot()
    : BindlessGpuDebugLineSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessGpuDebugLineSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessGpuDebugLineSlot&) const = default;
};

struct BindlessGpuDebugCounterSlot {
  ShaderVisibleIndex value;
  BindlessGpuDebugCounterSlot()
    : BindlessGpuDebugCounterSlot(kInvalidShaderVisibleIndex)
  {
  }
  explicit constexpr BindlessGpuDebugCounterSlot(const ShaderVisibleIndex v)
    : value(v)
  {
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BindlessGpuDebugCounterSlot&) const
    = default;
};

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
 per-item transforms now accessed via stable TransformHandle indirection
 and will be bound/consumed downstream.

 Fields contained in the GPU snapshot include:
   - view_matrix / projection_matrix: Camera basis.
   - camera_position: World-space camera origin.
   - time_seconds: Accumulated time (seconds) for temporal effects.
   - frame_index: Monotonic frame counter wrapped in a strong type (FrameIndex).
   - bindless_draw_metadata_slot: Shader-visible descriptor slot for
 DrawMetadata structured buffer (BindlessDrawMetadataSlot); use
 kInvalidShaderVisibleIndex when unavailable.

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

  // ReSharper disable CppInconsistentNaming
  struct alignas(16) GpuData {
    glm::mat4 view_matrix { 1.0F };
    glm::mat4 projection_matrix { 1.0F };

    // Aligned at 16 bytes here
    glm::vec3 camera_position { 0.0F, 0.0F, 0.0F };
    frame::Slot::UnderlyingType frame_slot { 0 };

    // Aligned at 16 bytes here
    frame::SequenceNumber frame_seq_num { 0 };
    // Aligned at 8 bytes here
    float time_seconds { 0.0F };
    uint32_t _pad0 { 0 }; // padding - do not use

    // Aligned at 16 bytes here
    BindlessDrawMetadataSlot bindless_draw_metadata_slot;
    BindlessWorldsSlot bindless_transforms_slot;
    BindlessNormalsSlot bindless_normal_matrices_slot;
    BindlessMaterialConstantsSlot bindless_material_constants_slot;

    // Aligned at 16 bytes here
    BindlessEnvironmentStaticSlot bindless_env_static_slot;
    BindlessDirectionalLightsSlot bindless_directional_lights_slot;
    BindlessDirectionalShadowsSlot bindless_directional_shadows_slot;
    BindlessPositionalLightsSlot bindless_positional_lights_slot;

    // Aligned at 16 bytes here
    BindlessInstanceDataSlot bindless_instance_data_slot;
    BindlessGpuDebugLineSlot bindless_gpu_debug_line_slot;
    BindlessGpuDebugCounterSlot bindless_gpu_debug_counter_slot;
    uint32_t _pad1 { 0 }; // padding to 16-byte alignment
    // Aligned at 16 bytes here

    // Additional padding to match HLSL cbuffer packing (DXC reflects 256 bytes)
    // HLSL's uint64_t has stricter alignment requirements that cause different
    // packing than C++. We add explicit padding to ensure the C++ struct
    // matches what the GPU expects.
    uint32_t _pad[12] { 0 }; // 48 bytes: 208 + 48 = 256
  };
  // ReSharper restore CppInconsistentNaming
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(GpuData) % 16 == 0, "GpuData not 16-byte aligned");
  static_assert(sizeof(GpuData) == 256, "GpuData HLSL cbuffer packing not 256");
  // NOLINTEND(*-magic-numbers)

  SceneConstants() = default;
  OXYGEN_DEFAULT_COPYABLE(SceneConstants)
  OXYGEN_DEFAULT_MOVABLE(SceneConstants)
  ~SceneConstants() = default;

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

  [[nodiscard]] constexpr auto GetBindlessDrawMetadataSlot() const noexcept
  {
    return bindless_draw_metadata_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessWorldsSlot() const noexcept
  {
    return bindless_transforms_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessNormalMatricesSlot() const noexcept
  {
    return bindless_normal_matrices_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessMaterialConstantsSlot() const noexcept
  {
    return bindless_material_constants_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessEnvironmentStaticSlot() const noexcept
  {
    return bindless_env_static_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessDirectionalLightsSlot() const noexcept
  {
    return bindless_directional_lights_slot_;
  }

  [[nodiscard]] constexpr auto
  GetBindlessDirectionalShadowsSlot() const noexcept
  {
    return bindless_directional_shadows_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessPositionalLightsSlot() const noexcept
  {
    return bindless_positional_lights_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessInstanceDataSlot() const noexcept
  {
    return bindless_instance_data_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessGpuDebugLineSlot() const noexcept
  {
    return bindless_gpu_debug_line_slot_;
  }

  [[nodiscard]] constexpr auto GetBindlessGpuDebugCounterSlot() const noexcept
  {
    return bindless_gpu_debug_counter_slot_;
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
      .view_matrix = view_matrix_,
      .projection_matrix = projection_matrix_,
      .camera_position = camera_position_,
      .frame_slot = frame_slot_.get(),
      .frame_seq_num = frame_seq_num_,
      .time_seconds = time_seconds_,
      .bindless_draw_metadata_slot = bindless_draw_metadata_slot_,
      .bindless_transforms_slot = bindless_transforms_slot_,
      .bindless_normal_matrices_slot = bindless_normal_matrices_slot_,
      .bindless_material_constants_slot = bindless_material_constants_slot_,

      .bindless_env_static_slot = bindless_env_static_slot_,
      .bindless_directional_lights_slot = bindless_directional_lights_slot_,
      .bindless_directional_shadows_slot = bindless_directional_shadows_slot_,
      .bindless_positional_lights_slot = bindless_positional_lights_slot_,

      .bindless_instance_data_slot = bindless_instance_data_slot_,
      .bindless_gpu_debug_line_slot = bindless_gpu_debug_line_slot_,
      .bindless_gpu_debug_counter_slot = bindless_gpu_debug_counter_slot_,
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
  BindlessDrawMetadataSlot bindless_draw_metadata_slot_;
  BindlessWorldsSlot bindless_transforms_slot_;
  BindlessNormalsSlot bindless_normal_matrices_slot_;
  BindlessMaterialConstantsSlot bindless_material_constants_slot_;

  BindlessEnvironmentStaticSlot bindless_env_static_slot_;
  BindlessDirectionalLightsSlot bindless_directional_lights_slot_;
  BindlessDirectionalShadowsSlot bindless_directional_shadows_slot_;
  BindlessPositionalLightsSlot bindless_positional_lights_slot_;
  BindlessInstanceDataSlot bindless_instance_data_slot_;
  BindlessGpuDebugLineSlot bindless_gpu_debug_line_slot_;
  BindlessGpuDebugCounterSlot bindless_gpu_debug_counter_slot_;

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
