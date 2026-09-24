//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include <Oxygen/Graphics/Common/RecordingUseBatch.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics {
RecordingUseBatch::RecordingUseBatch()
  : registrations_(std::size_t { 0 })
  , opaque_(std::size_t { 0 })
  , dependencies_(std::size_t { 0 })
{
}
RecordingUseBatch::~RecordingUseBatch()
{
  // Issued owners are explicitly completed or torn down by their queue before
  // pooled storage is destroyed. An unresolved recording has not reached issue.
  assert(!HasUses() || !resolved_ || outcome_ == SubmissionOutcome::kDiscarded);
  Release(UseReleaseReason::kDiscarded);
}

auto RecordingUseBatch::Bind(BackendIncarnationId backend, QueueIdentity queue)
  -> void
{
  if (HasUses() || !dependencies_.empty()) {
    throw std::logic_error("Cannot rebind an occupied recording use batch");
  }
  backend_ = backend;
  queue_ = queue;
  outcome_ = SubmissionOutcome::kDiscarded;
  resolved_ = false;
}
auto RecordingUseBatch::NeedsCompletion() const noexcept -> bool
{
  return !registrations_.empty()
    || std::ranges::any_of(
      opaque_, [](const auto& use) { return use.hooks.requires_completion; });
}

auto RecordingUseBatch::Contains(RegistrationIdentity id) const noexcept -> bool
{
  return std::ranges::any_of(
    registrations_, [&](const auto& pin) { return pin.Identity() == id; });
}
auto RecordingUseBatch::Retain(ResourceRegistry& registry,
  const RegistrationOwner& owner) -> std::expected<void, RegistrationError>
{
  if (resolved_) {
    return std::unexpected(RegistrationError::kClosed);
  }
  if (owner.Identity().backend != backend_) {
    return std::unexpected(RegistrationError::kWrongBackend);
  }
  if (Contains(owner.Identity())) {
    return {};
  }
  try {
    if (registrations_.size() == registrations_.capacity()) {
      registrations_.reserve(
        (std::max)(std::size_t { 8 }, registrations_.capacity() * 2));
    }
    auto use = registry.RetainUse(owner);
    if (!use) {
      return std::unexpected(use.error());
    }
    registrations_.push_back(std::move(*use));
    return {};
  } catch (...) {
    return std::unexpected(RegistrationError::kAllocationFailed);
  }
}
auto RecordingUseBatch::RetainOpaque(std::shared_ptr<const void> owner,
  uint64_t kind, void* context, OpaqueUseHooks hooks) -> void
{
  if (resolved_ || !owner) {
    throw std::logic_error("Invalid opaque recording use");
  }
  if (std::ranges::any_of(opaque_, [&](const auto& pin) {
        return pin.owner.get() == owner.get() && pin.kind == kind;
      })) {
    return;
  }
  if (opaque_.size() == opaque_.capacity()) {
    opaque_.reserve((std::max)(std::size_t { 8 }, opaque_.capacity() * 2));
  }
  if (hooks.prepare) {
    hooks.prepare(context, queue_);
  }
  opaque_.push_back({ std::move(owner), kind, context, hooks });
}
auto RecordingUseBatch::RecordDependency(CompletionReceipt receipt) -> void
{
  if (resolved_ || !receipt.IsValid() || receipt.Backend() != backend_) {
    throw std::logic_error("Invalid completion dependency");
  }
  // Even same-queue values are validated at submit; the queue omits their wait.
  for (auto& dependency : dependencies_) {
    if (dependency.Queue() == receipt.Queue()) {
      if (dependency.Value() < receipt.Value()) {
        dependency = receipt;
      }
      return;
    }
  }
  dependencies_.push_back(receipt);
}
auto RecordingUseBatch::Validate() const noexcept -> bool
{
  if (resolved_) {
    return false;
  }
  for (const auto& pin : registrations_) {
    if (!pin.CanSubmit()) {
      return false;
    }
  }
  for (const auto& pin : opaque_) {
    if (pin.hooks.valid && !pin.hooks.valid(pin.context)) {
      return false;
    }
  }
  return true;
}
auto RecordingUseBatch::ResolveSubmission(
  const SubmissionResult& result) noexcept -> void
{
  if (resolved_) {
    return;
  }
  resolved_ = true;
  outcome_ = result.outcome;
  if (outcome_ != SubmissionOutcome::kDiscarded) {
    for (auto& pin : opaque_) {
      if (pin.hooks.submitted) {
        pin.hooks.submitted(pin.context, queue_, result);
      }
    }
  }
}
auto RecordingUseBatch::InvalidateRegistrations(
  ResourceRegistry& registry) noexcept -> void
{
  for (const auto& pin : registrations_) {
    const auto id = pin.Identity();
    registry.InvalidateManagedRegistrations({ &id, 1 });
  }
}

auto RecordingUseBatch::Release(UseReleaseReason reason) noexcept -> void
{
  for (auto& pin : opaque_) {
    if (pin.hooks.released) {
      pin.hooks.released(pin.context, queue_, outcome_, reason);
    }
  }
  opaque_.clear();
  registrations_.clear();
  dependencies_.clear();
  resolved_ = false;
  outcome_ = SubmissionOutcome::kDiscarded;
}
} // namespace oxygen::graphics
