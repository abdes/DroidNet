//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {

class BackendLifetime;
enum class SubmissionOutcome : uint8_t;

class CommandList;
class CommandRecorder;
namespace detail {
  class DeferredReclaimer;
}
namespace internal {
  class Commander;
}

//! Determines whether a recording submits on successful scope exit.
enum class SubmissionPolicy : uint8_t {
  kOnScopeExit,
  kExplicit,
};

//! Owns a recording independently of the passes that borrow its recorder.
/*!
 Automatic recordings submit on normal scope exit. Explicit recordings and
 recordings destroyed during exception unwinding discard unfinished work.
 Submit() and Discard() resolve the owner exactly once. Borrowed recorder
 references are valid only while the recording is unresolved. Backend-acquired
 recordings retain the canonical backend until resolution; a resolved shell can
 outlive it. Submission does not imply GPU completion.
*/
class CommandRecording final {
public:
  OXGN_GFX_API CommandRecording() noexcept;
  OXGN_GFX_API ~CommandRecording() noexcept;
  OXGN_GFX_API CommandRecording(CommandRecording&& other) noexcept;
  OXGN_GFX_API auto operator=(CommandRecording&& other) noexcept
    -> CommandRecording&;
  OXYGEN_MAKE_NON_COPYABLE(CommandRecording)

  //! Tests whether commands may still be recorded through this owner.
  OXGN_GFX_API explicit operator bool() const noexcept;
  //! Borrows the active recorder; the owner retains its lifetime.
  OXGN_GFX_NDAPI auto operator*() const -> CommandRecorder&;
  //! Borrows the active recorder; the owner retains its lifetime.
  OXGN_GFX_NDAPI auto operator->() const -> CommandRecorder*;

  //! Closes and submits once, reporting actual submission success.
  [[nodiscard]] OXGN_GFX_API auto Submit() noexcept -> bool;
  [[nodiscard]] OXGN_GFX_API auto SubmitWithReceipt() noexcept
    -> SubmissionResult;
  [[nodiscard]] auto Result() const noexcept -> SubmissionResult
  {
    return result_;
  }
  //! Discards unfinished work and resolves pending publication as discarded.
  OXGN_GFX_API void Discard() noexcept;

private:
  friend class internal::Commander;
  CommandRecording(std::unique_ptr<CommandRecorder> recorder,
    observer_ptr<detail::DeferredReclaimer> reclaimer, SubmissionPolicy policy,
    std::shared_ptr<Graphics> backend_owner,
    std::shared_ptr<BackendLifetime> lifetime);
  auto Finalize(bool receipt) noexcept -> SubmissionResult;
  void FinishScope() noexcept;
  void ResolveAndRelease(SubmissionOutcome outcome) noexcept;

  enum class State : uint8_t {
    kEmpty,
    kRecording,
    kSubmitted,
    kExecutionUncertain,
    kDiscarded,
  };

  // Recorder and list references must die before the canonical owner token.
  std::shared_ptr<Graphics> backend_owner_;
  std::shared_ptr<BackendLifetime> lifetime_;
  std::unique_ptr<CommandRecorder> recorder_;
  std::shared_ptr<CommandList> command_list_;
  observer_ptr<detail::DeferredReclaimer> reclaimer_;
  SubmissionPolicy policy_ { SubmissionPolicy::kOnScopeExit };
  State state_ { State::kEmpty };
  int uncaught_exceptions_ {};
  bool ended_ { false };
  SubmissionResult result_;
};

} // namespace oxygen::graphics
