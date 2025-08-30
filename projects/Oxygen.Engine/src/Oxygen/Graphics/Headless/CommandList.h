//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <deque>
#include <memory>
#include <optional>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {

class Graphics;

//! Headless backend concrete CommandList.
/*!
 This class provides a simple in-memory command stream used by the headless
 backend. Recorded commands are stored in a FIFO container and may be moved into
 a submission task for execution by the queue's serial executor.

 The API intentionally mirrors GPU-like behavior:
 - Commands may be queued while the CommandList is in the Recording state.
 - Commands may be dequeued (popped) while the CommandList is in the
   Submitted/Executing state by the worker executing the submission.

 @note Thread-safety: The class itself does not provide internal locking for
 access to the command container. The headless submission path moves the
 container into a worker thread; callers that access the API concurrently must
 ensure external synchronization.
*/
class CommandList final : public ::oxygen::graphics::CommandList {
public:
  OXGN_HDLS_API CommandList(std::string_view name, QueueRole role);
  OXGN_HDLS_API ~CommandList() override;

  OXGN_HDLS_API CommandList(const CommandList&) = delete;
  OXGN_HDLS_API CommandList& operator=(const CommandList&) = delete;
  OXGN_HDLS_API CommandList(CommandList&&) = delete;
  OXGN_HDLS_API CommandList& operator=(CommandList&&) = delete;

  //! Queue a command while recording. Throws if not recording.
  OXGN_HDLS_API auto QueueCommand(std::shared_ptr<Command> cmd) -> void;

  //! Pop the next command for execution while submitted/executing.
  /*! Returns nullopt if there are no commands remaining. */
  OXGN_HDLS_NDAPI auto DequeueCommand()
    -> std::optional<std::shared_ptr<Command>>;

  //! Peek at the next command without removing it.
  OXGN_HDLS_NDAPI auto PeekNext() const
    -> std::optional<std::shared_ptr<Command>>;

  //! Clear all recorded commands. Allowed in Recording or Closed state.
  OXGN_HDLS_API auto Clear() -> void;

  //! Steal all recorded commands by moving out the internal deque.
  /*! This transfers ownership of the recorded commands to the caller. The
      internal container will be left empty. Allowed when the command list is
      Submitted/Executing; throws otherwise. */
  OXGN_HDLS_API auto StealCommands() -> std::deque<std::shared_ptr<Command>>;

  OXGN_HDLS_API auto OnBeginRecording() -> void override;
  OXGN_HDLS_API auto OnEndRecording() -> void override;
  OXGN_HDLS_API auto OnSubmitted() -> void override;
  OXGN_HDLS_API auto OnExecuted() -> void override;

private:
  std::deque<std::shared_ptr<Command>> commands_;
};

} // namespace oxygen::graphics::headless
