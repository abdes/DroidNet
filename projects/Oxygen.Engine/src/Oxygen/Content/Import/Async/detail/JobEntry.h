//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/OxCo/Event.h>

namespace oxygen::content::import::detail {

//! Entry for a single import job in the job channel.
struct JobEntry {
  //! Unique job identifier.
  ImportJobId job_id = kInvalidJobId;

  //! Import request with source path and options.
  ImportRequest request;

  //! Callback for completion notification.
  ImportCompletionCallback on_complete;

  //! Optional callback for progress updates.
  ImportProgressCallback on_progress;

  //! Event to signal cancellation request for this job.
  std::shared_ptr<co::Event> cancel_event;
};

} // namespace oxygen::content::import::detail
