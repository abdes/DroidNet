//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once
#include <array>
#include <variant>
#include <vector>

#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/ProjectedLocalShadowRecord.h>

namespace oxygen::vortex::shadows::internal {
inline constexpr float kLocalShadowSlopeDepthBiasScale = 3.0F;
enum class LocalShadowDepthContract : std::uint8_t {
  kProjectedLinearReversedV1,
  kCubeRasterReversedV1,
};
//! Immutable depth inputs, independent of view, frame, selection and storage.
struct LocalShadowContentKey {
  std::uint64_t scene_generation { 0 };
  scene::NodeHandle light;
  std::uint32_t resolution { 0 };
  Format format { Format::kDepth32 };
  LocalShadowDepthContract contract {
    LocalShadowDepthContract::kProjectedLinearReversedV1
  };
  std::array<glm::mat4, 6> matrices { glm::mat4(0), glm::mat4(0), glm::mat4(0),
    glm::mat4(0), glm::mat4(0), glm::mat4(0) };
  glm::vec4 bias { 0 };
  glm::vec4 direction { 0 };
  glm::vec4 position_and_inverse_range { 0 };
  std::vector<std::shared_ptr<const ShadowCasterRecord>> casters;
  std::uint64_t hash { 0 };
  bool reusable { false };
  [[nodiscard]] OXGN_VRTX_API auto operator==(
    const LocalShadowContentKey& other) const -> bool;
};
struct LocalShadowRequest {
  LightSelectionIndex selection { kInvalidLightSelectionIndex };
  float strength { 1 };
  std::vector<std::uint64_t> caster_hash_scratch;
  LocalShadowContentKey content;
  std::variant<ProjectedLocalShadowRecord, CubeLocalShadowRecord> projection;
  [[nodiscard]] auto IsCube() const noexcept -> bool
  {
    return content.contract == LocalShadowDepthContract::kCubeRasterReversedV1;
  }
};
OXGN_VRTX_API auto PrepareLocalShadowRequest(
  const PreparedViewShadowInput& view, const FrameLocalLightSelection& light,
  LightSelectionIndex selection, std::uint32_t resolution, float strength,
  LocalShadowRequest& request) -> void;
}
