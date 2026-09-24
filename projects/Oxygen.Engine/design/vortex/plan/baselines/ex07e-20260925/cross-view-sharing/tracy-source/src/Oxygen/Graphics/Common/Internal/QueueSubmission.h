//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Submission.h>

namespace oxygen::graphics {
class CommandQueue;
namespace internal {
  struct ResolvedQueueDependency {
    const CommandQueue* producer;
    CompletionReceipt receipt;
  };
  struct NativeSubmissionRequest {
    std::span<const std::shared_ptr<CommandList>> lists;
    std::span<const ResolvedQueueDependency> dependencies;
    std::optional<uint64_t> private_marker;
    std::optional<uint64_t> legacy_marker;
    bool fail_before_private_marker { false };
    bool fail_after_first_list { false };
  };
  struct NativeSubmissionProgress {
    size_t issued_lists { 0 };
    uint64_t last_legacy_signal { 0 };
    bool private_marker_emitted { false };
  };
  //! Preparation may allocate. Execute must retain progress before a fallible
  //! post-issue operation; ownership bookkeeping afterward is allocation-free.
  class NativeSubmission {
  public:
    virtual ~NativeSubmission() = default;
    virtual auto Execute(NativeSubmissionProgress& progress) -> void = 0;
  };
  struct SubmittedWork {
    std::vector<std::shared_ptr<CommandList>> lists;
    std::vector<CommandList::RecordedResourceState> final_states;
    std::vector<size_t> state_ends;
    std::vector<ResolvedQueueDependency> dependencies;
    std::unique_ptr<NativeSubmission> native;
    SubmissionResult result;
    size_t issued_lists { 0 };
    bool published { false };
    std::unique_ptr<SubmittedWork> next;
  };
} // namespace internal
} // namespace oxygen::graphics
