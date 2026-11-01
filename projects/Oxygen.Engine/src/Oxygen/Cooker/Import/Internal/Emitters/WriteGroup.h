//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import {

//! The writes one emitter has in flight: tasks of the import session's
//! nursery, joined by `Join` with the first error.
/*!
 An emitter starts each cooked write as a task of the session's nursery,
 keeping the data alive in the coroutine, and awaits `Join` in its
 `Finalize`. Cancelling the nursery (the session's job was cancelled)
 cancels the writes through the driver; the nursery joins them before the
 session's storage goes away.

 Every error is counted; the first one is kept and `Join` returns it.

 ### Thread Safety

 Import thread only, like the emitters that own a group.
*/
//! A cooked write that failed: the file it wrote and why.
struct WriteFailure {
  std::filesystem::path path;
  std::error_code error;

  //! The file and the reason, for a diagnostic.
  [[nodiscard]] auto Message() const -> std::string
  {
    return path.generic_string() + ": " + error.message();
  }
};

class WriteGroup final {
public:
  //! A group whose writes run in `nursery`, which outlives every write.
  explicit WriteGroup(co::Nursery& nursery) noexcept
    : nursery_(make_observer(&nursery))
  {
  }

  ~WriteGroup() = default;

  OXYGEN_MAKE_NON_COPYABLE(WriteGroup)
  OXYGEN_MAKE_NON_MOVABLE(WriteGroup)

  //! Starts `write` as a task of the nursery. It records its own errors
  //! through `Record`; the result it returns is recorded too.
  //! Starts `write` of the file `path`.
  auto Start(std::filesystem::path path, co::Co<Result<void>> write) -> void
  {
    ++running_;
    nursery_->Start([this, path = std::move(path),
                      write = std::move(write)] mutable -> co::Co<> {
      const auto finished = ScopeGuard([this] noexcept -> void { Finish(); });
      Record(path, co_await write);
    });
  }

  //! Counts `result` when it is an error and keeps the first one.
  //! Records the outcome of a write of the file `path`.
  auto Record(const std::filesystem::path& path, const Result<void>& result)
    -> void
  {
    if (result) {
      return;
    }
    Fail(path, result.error());
  }

  //! Records a failure that started no write.
  //! Records a write of the file `path` that failed before it started.
  auto Fail(const std::filesystem::path& path, const std::error_code error)
    -> void
  {
    ++error_count_;
    if (!first_failure_) {
      first_failure_ = WriteFailure { .path = path, .error = error };
    }
  }

  //! Waits for the started writes, and returns the first error any write
  //! recorded. No write starts after it.
  //! Waits for every write started; the first failure, if any.
  [[nodiscard]] auto Join() -> co::Co<Result<void, WriteFailure>>
  {
    joining_ = true;
    if (running_ > 0) {
      co_await idle_;
    }
    if (first_failure_) {
      co_return Result<void, WriteFailure>::Err(*first_failure_);
    }
    co_return Result<void, WriteFailure> {};
  }

  //! The number of errors recorded.
  [[nodiscard]] auto ErrorCount() const noexcept -> size_t
  {
    return error_count_;
  }

  //! The number of writes started and not finished.
  [[nodiscard]] auto Running() const noexcept -> size_t { return running_; }

private:
  auto Finish() noexcept -> void
  {
    if (--running_ == 0 && joining_) {
      idle_.Trigger();
    }
  }

  observer_ptr<co::Nursery> nursery_;
  size_t running_ { 0 };
  size_t error_count_ { 0 };
  std::optional<WriteFailure> first_failure_;
  bool joining_ { false };
  co::Event idle_;
};

} // namespace oxygen::content::import
