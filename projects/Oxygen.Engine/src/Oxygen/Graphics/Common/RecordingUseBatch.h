//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/Submission.h>

namespace oxygen::graphics {
//! Completion hooks for an opaque use owner, without a Graphics dependency.
/*!
 prepare runs once before recording references the owner and may allocate; it
 must preserve its old state if it throws. Later hooks must not allocate or
 throw. The recording thread invokes submitted outside queue/lifecycle locks;
 retirement invokes released on the Graphics owner thread (or after close for
 CPU cleanup).
*/
struct OpaqueUseHooks {
  void (*prepare)(void*, QueueIdentity) { nullptr };
  bool (*valid)(const void*) noexcept { nullptr };
  void (*submitted)(
    void*, QueueIdentity, const SubmissionResult&) noexcept { nullptr };
  void (*released)(void*, QueueIdentity, SubmissionOutcome,
    UseReleaseReason) noexcept { nullptr };
  //! False only for CPU admission guards that retain no GPU resources.
  bool requires_completion { true };
};

//! Pooled recording storage. Submission transfers internal pins, never leases.
class RecordingUseBatch final {
public:
  OXGN_GFX_API RecordingUseBatch();
  OXGN_GFX_API ~RecordingUseBatch();
  OXYGEN_MAKE_NON_COPYABLE(RecordingUseBatch)
  OXYGEN_MAKE_NON_MOVABLE(RecordingUseBatch)

  OXGN_GFX_API auto Bind(BackendIncarnationId backend, QueueIdentity queue)
    -> void;
  OXGN_GFX_API auto Retain(ResourceRegistry& registry,
    const RegistrationOwner& owner) -> Result<void, RegistrationError>;
  OXGN_GFX_API auto RetainOpaque(std::shared_ptr<const void> owner,
    uint64_t kind, void* context = nullptr, OpaqueUseHooks hooks = {}) -> void;
  OXGN_GFX_API auto RecordDependency(CompletionReceipt receipt) -> void;
  [[nodiscard]] OXGN_GFX_API auto Validate() const noexcept -> bool;
  [[nodiscard]] auto MatchesQueue(
    BackendIncarnationId backend, QueueIdentity queue) const noexcept -> bool
  {
    return backend_ == backend && queue_ == queue;
  }
  [[nodiscard]] OXGN_GFX_API auto NeedsCompletion() const noexcept -> bool;
  [[nodiscard]] auto RegistrationCount() const noexcept -> size_t
  {
    return registrations_.size();
  }
  [[nodiscard]] auto OpaqueUseCount() const noexcept -> size_t
  {
    return opaque_.size();
  }
  [[nodiscard]] auto HasUses() const noexcept -> bool
  {
    return !registrations_.empty() || !opaque_.empty();
  }
  [[nodiscard]] auto Dependencies() const noexcept
    -> std::span<const CompletionReceipt>
  {
    return dependencies_;
  }
  [[nodiscard]] OXGN_GFX_API auto Contains(
    RegistrationIdentity id) const noexcept -> bool;
  OXGN_GFX_API auto ResolveSubmission(const SubmissionResult& result) noexcept
    -> void;
  OXGN_GFX_API auto InvalidateRegistrations(ResourceRegistry& registry) noexcept
    -> void;
  OXGN_GFX_API auto Release(UseReleaseReason reason) noexcept -> void;

private:
  struct OpaqueEntry {
    std::shared_ptr<const void> owner;
    uint64_t kind;
    void* context;
    OpaqueUseHooks hooks;
  };
  BackendIncarnationId backend_ { 0 };
  QueueIdentity queue_ { 0 };
  SubmissionOutcome outcome_ { SubmissionOutcome::kDiscarded };
  bool resolved_ { false };
  std::vector<RegistrationUse> registrations_;
  std::vector<OpaqueEntry> opaque_;
  std::vector<CompletionReceipt> dependencies_;
};
} // namespace oxygen::graphics
