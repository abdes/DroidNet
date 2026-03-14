//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <Oxygen/Renderer/Types/ViewConstants.h>

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

auto ViewConstants::SetViewMatrix(const glm::mat4& m) noexcept -> ViewConstants&
{
  if (!Mat4Equal(view_matrix_, m)) {
    view_matrix_ = m;
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::SetProjectionMatrix(const glm::mat4& m) noexcept
  -> ViewConstants&
{
  bool changed = false;
  if (!Mat4Equal(projection_matrix_, m)) {
    projection_matrix_ = m;
    changed = true;
  }
  if (!stable_projection_explicit_
    && !Mat4Equal(stable_projection_matrix_, m)) {
    stable_projection_matrix_ = m;
    changed = true;
  }
  if (changed) {
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::SetStableProjectionMatrix(const glm::mat4& m) noexcept
  -> ViewConstants&
{
  if (!stable_projection_explicit_
    || !Mat4Equal(stable_projection_matrix_, m)) {
    stable_projection_matrix_ = m;
    stable_projection_explicit_ = true;
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::SetCameraPosition(const glm::vec3& p) noexcept
  -> ViewConstants&
{
  if (glm::any(glm::notEqual(camera_position_, p))) {
    camera_position_ = p;
    version_ = version_.Next();
  }
  return *this;
}
auto ViewConstants::SetTimeSeconds(const float t, RendererTag /*tag*/) noexcept
  -> ViewConstants&
{
  if (std::abs(time_seconds_ - t) > glm::epsilon<float>()) {
    time_seconds_ = t;
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::SetFrameSlot(
  const frame::Slot slot, RendererTag /*tag*/) noexcept -> ViewConstants&
{
  if (frame_slot_ != slot) {
    frame_slot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::SetFrameSequenceNumber(const frame::SequenceNumber seq,
  RendererTag /*tag*/) noexcept -> ViewConstants&
{
  if (frame_seq_num_ != seq) {
    frame_seq_num_ = seq;
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::SetBindlessViewFrameBindingsSlot(
  const BindlessViewFrameBindingsSlot slot, RendererTag /*tag*/) noexcept
  -> ViewConstants&
{
  if (view_frame_bindings_bslot_ != slot) {
    view_frame_bindings_bslot_ = slot;
    version_ = version_.Next();
  }
  return *this;
}

auto ViewConstants::GetSnapshot() const noexcept -> const GpuData&
{
  if (cached_version_ != version_) {
    RebuildCache();
    cached_version_ = version_;
  }
  return cached_;
}

} // namespace oxygen::engine
