//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>

namespace oxygen::vortex::shadows::internal {
namespace {
  auto FindQueue(SharedShadowBacking& backing, graphics::QueueIdentity queue)
    -> SharedShadowBacking::QueueUse&
  {
    const auto found = std::ranges::find(
      backing.queues, queue, &SharedShadowBacking::QueueUse::queue);
    assert(found != backing.queues.end());
    return *found;
  }
  auto PrepareUse(void* pointer, graphics::QueueIdentity queue) -> void
  {
    const auto& context = *static_cast<ShadowMapVersion::UseContext*>(pointer);
    auto& version = *context.version;
    auto& backing = *version.slot->backing;
    std::lock_guard lock(backing.mutex);
    if (backing.quarantined || !version.accepting
      || (context.mode != ShadowUseMode::kWrite
        && version.state != ShadowMapVersion::State::kSubmitted)) {
      throw ShadowUseUnavailable(backing.quarantined || !version.accepting
          ? ShadowUseError::kClosed
          : ShadowUseError::kNotReady);
    }
    const auto exclusive = context.mode != ShadowUseMode::kRead;
    for (const auto& use : backing.queues) {
      if (use.pending_exclusive != 0 || (exclusive && use.pending_reads != 0)) {
        throw ShadowUseUnavailable(ShadowUseError::kNotReady);
      }
    }
    if (std::ranges::find(
          backing.queues, queue, &SharedShadowBacking::QueueUse::queue)
      == backing.queues.end()) {
      backing.queues.push_back(
        { .queue = queue }); // Before publishing any pin.
    }
    version.slot->AddUse();
    auto& use = FindQueue(backing, queue);
    if (exclusive) {
      ++use.pending_exclusive;
    } else {
      ++use.pending_reads;
    }
  }
  auto ValidateUse(const void* pointer) noexcept -> bool
  {
    const auto& context
      = *static_cast<const ShadowMapVersion::UseContext*>(pointer);
    const auto& version = *context.version;
    std::lock_guard lock(version.slot->backing->mutex);
    return !version.slot->backing->quarantined
      && (context.mode == ShadowUseMode::kWrite
          ? version.state == ShadowMapVersion::State::kRecording
          : version.state == ShadowMapVersion::State::kSubmitted);
  }
  auto SubmittedUse(void* pointer, graphics::QueueIdentity queue,
    const graphics::SubmissionResult& result) noexcept -> void
  {
    const auto& context = *static_cast<ShadowMapVersion::UseContext*>(pointer);
    auto& version = *context.version;
    auto& backing = *version.slot->backing;
    std::lock_guard lock(backing.mutex);
    auto& use = FindQueue(backing, queue);
    if (context.mode == ShadowUseMode::kRead) {
      --use.pending_reads;
    } else {
      --use.pending_exclusive;
    }
    ++use.submitted;
    if (result.outcome == graphics::SubmissionOutcome::kExecutionUncertain) {
      backing.quarantined = true;
      version.state = ShadowMapVersion::State::kQuarantined;
      return;
    }
    assert(result.receipt);
    use.latest = result.receipt;
    backing.has_submission = true;
    if (context.mode != ShadowUseMode::kRead) {
      backing.latest_exclusive = result.receipt;
    }
    if (context.mode == ShadowUseMode::kWrite) {
      version.producer = result.receipt;
      version.state = ShadowMapVersion::State::kSubmitted;
    }
  }
  auto ReleasedUse(void* pointer, graphics::QueueIdentity queue,
    graphics::SubmissionOutcome outcome,
    graphics::UseReleaseReason reason) noexcept -> void
  {
    const auto& context = *static_cast<ShadowMapVersion::UseContext*>(pointer);
    auto& version = *context.version;
    auto& backing = *version.slot->backing;
    {
      std::lock_guard lock(backing.mutex);
      auto& use = FindQueue(backing, queue);
      if (outcome == graphics::SubmissionOutcome::kDiscarded) {
        if (context.mode == ShadowUseMode::kRead) {
          --use.pending_reads;
        } else {
          --use.pending_exclusive;
        }
        if (context.mode == ShadowUseMode::kWrite) {
          version.state = ShadowMapVersion::State::kInvalid;
        }
      } else {
        --use.submitted;
      }
      if (reason == graphics::UseReleaseReason::kDeviceLost) {
        backing.quarantined = true;
        version.state = ShadowMapVersion::State::kQuarantined;
      }
    }
    version.slot->ReleaseUse();
  }
}

ShadowSlotCore::~ShadowSlotCore()
{
  assert(owners == 0 && uses == 0);
  RetireAndFinalize();
}
auto ShadowSlotCore::AddOwner() -> void
{
  std::lock_guard lock(mutex);
  if (retirement || finalized) {
    throw std::logic_error("Shadow slot has retired");
  }
  ++owners;
}
auto ShadowSlotCore::ReleaseOwner() noexcept -> void
{
  std::lock_guard lock(mutex);
  assert(owners != 0);
  --owners;
  RetireAndFinalize();
}
auto ShadowSlotCore::AddUse() -> void
{
  std::lock_guard lock(mutex);
  if (owners == 0 || retirement || finalized) {
    throw std::logic_error("Shadow slot has no allocation owner");
  }
  ++uses;
}
auto ShadowSlotCore::ReleaseUse() noexcept -> void
{
  std::lock_guard lock(mutex);
  assert(uses != 0);
  --uses;
  RetireAndFinalize();
}
auto ShadowSlotCore::RetireAndFinalize() noexcept -> void
{
  if (owners != 0 || finalized) {
    return;
  }
  auto target = pool.lock();
  if (!retirement && target) {
    auto ticket = target->reuse.TryRetire(handle);
    if (ticket) {
      retirement.emplace(std::move(*ticket));
    }
  }
  if (uses != 0) {
    return;
  }
  finalized = true;
  if (retirement) {
    const auto result = retirement->Finalize();
    retirement.reset();
    if (target && result.index) {
      std::lock_guard target_lock(target->mutex);
      if (!target->closed) {
        target->free.push_back(*result.index);
      }
    }
  }
}
ShadowMapVersion::ShadowMapVersion(
  std::shared_ptr<ShadowSlotCore> allocation, LocalShadowContentKey key)
  : slot(std::move(allocation))
  , content(std::move(key))
  , use_contexts { { { this, ShadowUseMode::kRead },
      { this, ShadowUseMode::kWrite }, { this, ShadowUseMode::kReadback } } }
{
}
ShadowMapOwner::ShadowMapOwner(std::shared_ptr<ShadowMapVersion> map)
  : version(std::move(map))
{
  version->slot->AddOwner();
}
ShadowMapOwner::~ShadowMapOwner() { version->slot->ReleaseOwner(); }
ShadowReadCapability::ShadowReadCapability(
  std::shared_ptr<ShadowMapOwner> owner)
  : owner_(std::move(owner))
{
  if (owner_) {
    std::lock_guard lock(owner_->version->slot->backing->mutex);
    if (!owner_->version->accepting
      || owner_->version->state != ShadowMapVersion::State::kSubmitted) {
      throw std::logic_error("Shadow version is closed for reader acquisition");
    }
    ++owner_->version->read_capabilities;
  }
}
ShadowReadCapability::ShadowReadCapability(const ShadowReadCapability& other)
  : ShadowReadCapability(other.owner_)
{
}
ShadowReadCapability::~ShadowReadCapability()
{
  if (owner_) {
    std::lock_guard lock(owner_->version->slot->backing->mutex);
    --owner_->version->read_capabilities;
  }
}
auto ShadowReadCapability::operator=(ShadowReadCapability other) noexcept
  -> ShadowReadCapability&
{
  owner_.swap(other.owner_);
  return *this;
}
auto CanWriteBacking(const SharedShadowBacking& backing) -> bool
{
  std::lock_guard lock(backing.mutex);
  return !backing.quarantined
    && std::ranges::none_of(backing.queues, [](const auto& use) {
         return use.pending_reads != 0 || use.pending_exclusive != 0;
       });
}
auto CanReplaceVersion(const ShadowMapVersion& version) -> bool
{
  std::lock_guard lock(version.slot->backing->mutex);
  return version.read_capabilities == 0 && !version.slot->backing->quarantined
    && std::ranges::none_of(version.slot->backing->queues, [](const auto& use) {
         return use.pending_reads != 0 || use.pending_exclusive != 0;
       });
}
auto AttachShadowUse(const std::shared_ptr<ShadowMapVersion>& version,
  ShadowUseMode mode, graphics::CommandRecorder& recorder,
  graphics::ResourceRegistry& registry) -> void
{
  const auto& slot = *version->slot;
  auto& backing = *slot.backing;
  if (mode == ShadowUseMode::kReadback
    && recorder.GetTargetQueue()->GetQueueRole()
      != graphics::QueueRole::kGraphics) {
    throw ShadowUseUnavailable(ShadowUseError::kWrongQueue);
  }
  const auto pin = recorder.RetainRegistration(registry, backing.registration);
  if (!pin) {
    throw ShadowUseUnavailable(
      pin.error() == graphics::RegistrationError::kWrongBackend
        ? ShadowUseError::kWrongBackend
        : pin.error() == graphics::RegistrationError::kAllocationFailed
        ? ShadowUseError::kAllocationFailed
        : ShadowUseError::kClosed);
  }
  const auto index = static_cast<size_t>(mode);
  recorder.RetainOpaqueUse(version, 0x5348445700ULL + index,
    &version->use_contexts[index],
    { .prepare = PrepareUse,
      .valid = ValidateUse,
      .submitted = SubmittedUse,
      .released = ReleasedUse });
  std::lock_guard lock(backing.mutex);
  if (version->producer) {
    recorder.RecordDependency(*version->producer);
  }
  if (backing.latest_exclusive) {
    recorder.RecordDependency(*backing.latest_exclusive);
  }
  if (mode != ShadowUseMode::kRead) {
    for (const auto& use : backing.queues) {
      if (use.latest) {
        recorder.RecordDependency(*use.latest);
      }
    }
  }
  recorder.BeginTrackingResourceState(*backing.texture,
    backing.has_submission ? graphics::ResourceStates::kShaderResource
                           : graphics::ResourceStates::kDepthWrite);
}
}
