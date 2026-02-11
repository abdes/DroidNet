//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace {
auto Mat4Equal(const glm::mat4& a, const glm::mat4& b, float epsilon = 0.0F)
  -> bool
{
  for (int i = 0; i < 4; ++i) {
    if (!glm::all(glm::epsilonEqual(a[i], b[i], epsilon))) {
      return false;
    }
  }
  return true;
}
} // namespace

namespace oxygen::engine {

auto SceneConstants::SetViewMatrix(const glm::mat4& m) noexcept
  -> SceneConstants&
{
  if (!Mat4Equal(view_matrix_, m)) {
    view_matrix_ = m;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetProjectionMatrix(const glm::mat4& m) noexcept
  -> SceneConstants&
{
  if (!Mat4Equal(projection_matrix_, m)) {
    projection_matrix_ = m;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetCameraPosition(const glm::vec3& p) noexcept
  -> SceneConstants&
{
  if (glm::any(glm::notEqual(camera_position_, p))) {
    camera_position_ = p;
    version_ = version_.Next();
  }
  return *this;
}
auto SceneConstants::SetTimeSeconds(const float t, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (std::abs(time_seconds_ - t) > glm::epsilon<float>()) {
    time_seconds_ = t;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetFrameSlot(
  const frame::Slot slot, RendererTag /*tag*/) noexcept -> SceneConstants&
{
  if (frame_slot_ != slot) {
    frame_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetFrameSequenceNumber(const frame::SequenceNumber seq,
  RendererTag /*tag*/) noexcept -> SceneConstants&
{
  if (frame_seq_num_ != seq) {
    frame_seq_num_ = seq;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetExposure(
  const float exposure, RendererTag /*tag*/) noexcept -> SceneConstants&
{
  if (std::abs(exposure_ - exposure) > glm::epsilon<float>()) {
    exposure_ = exposure;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessDrawMetadataSlot(
  const BindlessDrawMetadataSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (draw_metadata_bslot_ != slot) {
    draw_metadata_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessWorldsSlot(const BindlessWorldsSlot slot,
  RendererTag /*tag*/) noexcept -> SceneConstants&
{
  if (transforms_bslot_ != slot) {
    transforms_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessNormalMatricesSlot(
  const BindlessNormalsSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (normal_matrices_bslot_ != slot) {
    normal_matrices_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessMaterialConstantsSlot(
  const BindlessMaterialConstantsSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (material_constants_bslot_ != slot) {
    material_constants_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessEnvironmentStaticSlot(
  const BindlessEnvironmentStaticSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (env_static_bslot_ != slot) {
    env_static_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessDirectionalLightsSlot(
  const BindlessDirectionalLightsSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (directional_lights_bslot_ != slot) {
    directional_lights_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessDirectionalShadowsSlot(
  const BindlessDirectionalShadowsSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (directional_shadows_bslot_ != slot) {
    directional_shadows_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessPositionalLightsSlot(
  const BindlessPositionalLightsSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (positional_lights_bslot_ != slot) {
    positional_lights_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessInstanceDataSlot(
  const BindlessInstanceDataSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (instance_data_bslot_ != slot) {
    instance_data_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessGpuDebugLineSlot(
  const BindlessGpuDebugLineSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (gpu_debug_line_bslot_ != slot) {
    gpu_debug_line_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessGpuDebugCounterSlot(
  const BindlessGpuDebugCounterSlot slot, RendererTag /*tag*/) noexcept
  -> SceneConstants&
{
  if (gpu_debug_counter_bslot_ != slot) {
    gpu_debug_counter_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::GetSnapshot() const noexcept -> const GpuData&
{
  if (cached_version_ != version_) {
    RebuildCache();
    cached_version_ = version_;
  }
  return cached_;
}

} // namespace oxygen::engine
