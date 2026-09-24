//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>
#include <mutex>
#include <utility>

#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics {
auto RegistrationOwner::Identity() const noexcept -> RegistrationIdentity
{
  return owner_ ? owner_->core->identity : RegistrationIdentity {};
}
RegistrationUse::~RegistrationUse() { Reset(); }
RegistrationUse::RegistrationUse(RegistrationUse&& other) noexcept
  : core_(std::move(other.core_))
{
}
auto RegistrationUse::operator=(RegistrationUse&& other) noexcept
  -> RegistrationUse&
{
  if (this != &other) {
    Reset();
    core_ = std::move(other.core_);
  }
  return *this;
}
auto RegistrationUse::Identity() const noexcept -> RegistrationIdentity
{
  return core_ ? core_->identity : RegistrationIdentity {};
}

auto RegistrationUse::CanSubmit() const noexcept -> bool
{
  if (!core_) {
    return false;
  }
  const auto registry = core_->state.lock();
  if (!registry) {
    return false;
  }
  std::lock_guard lock(registry->mutex);
  return !registry->closed && core_->published && !core_->poisoned;
}
auto RegistrationUse::Reset() noexcept -> void
{
  if (auto core = std::move(core_)) {
    core->ReleaseUse();
  }
}

namespace detail {
  auto RegistrationCore::QueueIfReadyNoLock(
    ResourceRegistryState& registry) noexcept -> void
  {
    if (!published || queued || registry.closed || allocation_owners != 0
      || uses != 0) {
      return;
    }
    queued = true;
    ready_next = nullptr;
    if (registry.ready_tail) {
      registry.ready_tail->ready_next = this;
    } else {
      registry.ready_head = this;
    }
    registry.ready_tail = this;
  }
  auto RegistrationCore::ReleaseOwner() noexcept -> void
  {
    if (auto registry = state.lock()) {
      std::lock_guard lock(registry->mutex);
      assert(allocation_owners != 0);
      if (--allocation_owners == 0) {
        open = false;
        QueueIfReadyNoLock(*registry);
      }
    }
  }
  auto RegistrationCore::ReleaseUse() noexcept -> void
  {
    if (auto registry = state.lock()) {
      std::lock_guard lock(registry->mutex);
      assert(uses != 0);
      --uses;
      QueueIfReadyNoLock(*registry);
    }
  }
} // namespace detail
} // namespace oxygen::graphics
