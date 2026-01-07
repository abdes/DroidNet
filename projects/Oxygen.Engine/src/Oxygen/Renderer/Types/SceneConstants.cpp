//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace {
auto Mat4Equal(const glm::mat4& a, const glm::mat4& b, float epsilon = 0.0f)
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
auto SceneConstants::SetTimeSeconds(const float t, RendererTag) noexcept
  -> SceneConstants&
{
  if (std::abs(time_seconds_ - t) > glm::epsilon<float>()) {
    time_seconds_ = t;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetFrameSlot(const frame::Slot slot, RendererTag) noexcept
  -> SceneConstants&
{
  if (frame_slot_ != slot) {
    frame_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetFrameSequenceNumber(
  const frame::SequenceNumber seq, RendererTag) noexcept -> SceneConstants&
{
  if (frame_seq_num_ != seq) {
    frame_seq_num_ = seq;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessDrawMetadataSlot(
  const BindlessDrawMetadataSlot slot, RendererTag) noexcept -> SceneConstants&
{
  if (bindless_draw_metadata_slot_ != slot) {
    bindless_draw_metadata_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessWorldsSlot(
  const BindlessWorldsSlot slot, RendererTag) noexcept -> SceneConstants&
{
  if (bindless_transforms_slot_ != slot) {
    bindless_transforms_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessNormalMatricesSlot(
  const BindlessNormalsSlot slot, RendererTag) noexcept -> SceneConstants&
{
  if (bindless_normal_matrices_slot_ != slot) {
    bindless_normal_matrices_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessMaterialConstantsSlot(
  const BindlessMaterialConstantsSlot slot, RendererTag) noexcept
  -> SceneConstants&
{
  if (bindless_material_constants_slot_ != slot) {
    bindless_material_constants_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessEnvironmentStaticSlot(
  const BindlessEnvironmentStaticSlot slot, RendererTag) noexcept
  -> SceneConstants&
{
  if (bindless_env_static_slot_ != slot) {
    bindless_env_static_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessDirectionalLightsSlot(
  const BindlessDirectionalLightsSlot slot, RendererTag) noexcept
  -> SceneConstants&
{
  if (bindless_directional_lights_slot_ != slot) {
    bindless_directional_lights_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessDirectionalShadowsSlot(
  const BindlessDirectionalShadowsSlot slot, RendererTag) noexcept
  -> SceneConstants&
{
  if (bindless_directional_shadows_slot_ != slot) {
    bindless_directional_shadows_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessPositionalLightsSlot(
  const BindlessPositionalLightsSlot slot, RendererTag) noexcept
  -> SceneConstants&
{
  if (bindless_positional_lights_slot_ != slot) {
    bindless_positional_lights_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto SceneConstants::SetBindlessInstanceDataSlot(
  const BindlessInstanceDataSlot slot, RendererTag) noexcept -> SceneConstants&
{
  if (bindless_instance_data_slot_ != slot) {
    bindless_instance_data_slot_ = slot;
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
