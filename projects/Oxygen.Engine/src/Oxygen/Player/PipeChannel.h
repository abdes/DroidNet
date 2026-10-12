//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Oxygen/Base/Macros.h>

namespace oxygen::player {

//! The player's end of the launcher's named pipe: event lines out, request
//! lines in.
/*!
 Lines end with `\n`. `Send` may be called from any thread; a line is
 written whole. A reader thread collects request lines, which the engine
 thread takes with `TakeRequests`; nothing on that thread touches the engine.
 Once the launcher closes its end, `IsClosed` is true and sends are dropped.
*/
class PipeChannel final {
public:
  //! Connects to `\\.\pipe\<name>`, waiting up to `timeout` for the launcher
  //! to accept.
  /*!
   \throws std::runtime_error when the pipe cannot be opened.
  */
  [[nodiscard]] static auto Connect(std::string_view name,
    std::chrono::milliseconds timeout) -> std::unique_ptr<PipeChannel>;

  ~PipeChannel();

  OXYGEN_MAKE_NON_COPYABLE(PipeChannel)
  OXYGEN_MAKE_NON_MOVABLE(PipeChannel)

  //! Writes `line` and a line break. Never logs, so a log callback may call
  //! it.
  auto Send(std::string_view line) noexcept -> void;

  //! The request lines received since the last call, oldest first.
  [[nodiscard]] auto TakeRequests() -> std::vector<std::string>;

  [[nodiscard]] auto IsClosed() const noexcept -> bool
  {
    return closed_.load(std::memory_order_acquire);
  }

private:
  struct Handles;

  explicit PipeChannel(std::unique_ptr<Handles> handles);
  auto ReadLoop() noexcept -> void;

  std::unique_ptr<Handles> handles_;
  std::atomic_bool closed_ { false };
  std::mutex write_mutex_;
  std::mutex requests_mutex_;
  std::vector<std::string> requests_;
  std::thread reader_;
};

} // namespace oxygen::player
