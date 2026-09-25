//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <mutex>
#include <stdexcept>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowContentLease.h>

namespace oxygen::vortex {
using shadows::internal::ShadowReadCapability;
using shadows::internal::ShadowUseMode;
struct ShadowContentLease::State {
  graphics::RegistrationLease
    registration; // Canonical external backend ownership.
  ShadowReadCapability content;
};
ShadowContentLease::ShadowContentLease(
  std::shared_ptr<shadows::internal::ShadowMapOwner> map,
  graphics::RegistrationLease registration)
  : state_(std::make_shared<State>(
      std::move(registration), ShadowReadCapability(std::move(map))))
{
}
namespace {
  auto TryAttach(
    const std::shared_ptr<shadows::internal::ShadowMapVersion>& version,
    ShadowUseMode mode, graphics::CommandRecorder& recorder,
    graphics::ResourceRegistry& registry) -> std::expected<void, ShadowUseError>
  {
    try {
      AttachShadowUse(version, mode, recorder, registry);
      return {};
    } catch (const shadows::internal::ShadowUseUnavailable& error) {
      return std::unexpected(error.Error());
    } catch (const std::bad_alloc&) {
      return std::unexpected(ShadowUseError::kAllocationFailed);
    }
  }
}
auto ShadowContentLease::Attach(graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) const
  -> std::expected<void, ShadowUseError>
{
  const auto state = state_;
  if (!state) {
    return std::unexpected(ShadowUseError::kClosed);
  }
  return TryAttach(
    state->content.Owner()->version, ShadowUseMode::kRead, recorder, registry);
}
auto ShadowContentLease::AttachReadback(graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) const
  -> std::expected<void, ShadowUseError>
{
  const auto state = state_;
  if (!state) {
    return std::unexpected(ShadowUseError::kClosed);
  }
  return TryAttach(state->content.Owner()->version, ShadowUseMode::kReadback,
    recorder, registry);
}
auto ShadowContentLease::Texture() const
  -> std::shared_ptr<const graphics::Texture>
{
  return state_ ? state_->content.Owner()->version->slot->backing->texture
                : nullptr;
}
auto ShadowContentLease::FirstLayer() const -> std::uint32_t
{
  if (!state_) {
    throw std::logic_error("Empty shadow content lease");
  }
  const auto& slot = *state_->content.Owner()->version->slot;
  return slot.offset * (slot.backing->cube ? 6U : 1U);
}
struct ShadowFrameReadSet::State {
  mutable std::mutex mutex;
  frame::SequenceNumber frame;
  std::uint64_t revision;
  ShaderVisibleIndex binding;
  bool open { true };
  std::vector<ShadowReadCapability> maps;
  State(frame::SequenceNumber sequence, std::uint64_t preparation,
    ShaderVisibleIndex slot)
    : frame(sequence)
    , revision(preparation)
    , binding(slot)
  {
  }
};
ShadowFrameReadSet::ShadowFrameReadSet(frame::SequenceNumber frame,
  std::uint64_t preparation_revision, ShaderVisibleIndex binding,
  std::span<const std::shared_ptr<shadows::internal::ShadowMapOwner>> maps)
  : state_(std::make_shared<State>(frame, preparation_revision, binding))
{
  state_->maps.reserve(maps.size());
  for (const auto& map : maps) {
    state_->maps.emplace_back(map);
  }
}
ShadowFrameReadSet::~ShadowFrameReadSet() = default;
auto ShadowFrameReadSet::Close() noexcept -> void
{
  std::lock_guard lock(state_->mutex);
  state_->open = false;
  state_->maps.clear();
}
auto ShadowFrameReadSet::Attach(frame::SequenceNumber frame,
  std::uint64_t preparation_revision, graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) const
  -> std::expected<ShadowReadAttachmentStats, ShadowUseError>
{
  std::lock_guard lock(state_->mutex);
  if (!state_->open || state_->frame != frame
    || state_->revision != preparation_revision) {
    return std::unexpected(ShadowUseError::kClosed);
  }
  // Frame-ring bindings cannot authorize a late submission. Retained content
  // uses a separate lease with independently retained caller inputs.
  try {
    recorder.RetainOpaqueUse(state_, 0x53484652414D4500ULL, state_.get(),
      { .valid =
          [](const void* pointer) noexcept {
            const auto& state = *static_cast<const State*>(pointer);
            std::lock_guard guard(state.mutex);
            return state.open;
          },
        .requires_completion = false });
  } catch (const std::bad_alloc&) {
    return std::unexpected(ShadowUseError::kAllocationFailed);
  }
  const auto& batch = recorder.GetCommandListForInspection()->Uses();
  const auto registrations = batch.RegistrationCount();
  const auto versions = batch.OpaqueUseCount();
  for (const auto& map : state_->maps) {
    const auto attached = TryAttach(
      map.Owner()->version, ShadowUseMode::kRead, recorder, registry);
    if (!attached) {
      return std::unexpected(attached.error());
    }
  }
  return ShadowReadAttachmentStats { batch.OpaqueUseCount() - versions,
    batch.RegistrationCount() - registrations };
}
auto ShadowFrameReadSet::Binding() const noexcept -> ShaderVisibleIndex
{
  return state_->binding;
}
}
