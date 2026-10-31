//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! A file bound to an `IoDriver`: the blocking operations of a `NativeFile`
//! and awaitable ones.
/*!
 `IoDriver::Open` returns it. An awaited operation is submitted to the
 driver's backend when awaited and resumes the awaiting coroutine on the
 driver's loop thread; no thread blocks on the device. The blocking
 operations stay available for code that need not suspend, such as reading a
 header before the loop starts.

 Awaited operations run on the driver's loop thread, and every one completes
 before the file is destroyed; structured concurrency (a nursery, `AllOf`)
 guarantees it. The driver outlives the file.

 ```cpp
 auto file = driver.Open(path, { .access = Access::kRead });
 if (!file) { co_return Err(file.error()); }
 auto read = co_await file->AsyncReadAt(offset, buffer);
 ```
*/
class AsyncFile {
public:
  OXGN_SERIO_API ~AsyncFile() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(AsyncFile)

  OXGN_SERIO_API AsyncFile(AsyncFile&& other) noexcept;
  OXGN_SERIO_API auto operator=(AsyncFile&& other) noexcept -> AsyncFile&;

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return file_.Size();
  }

  [[nodiscard]] auto ReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    return file_.ReadAt(at, out);
  }

  [[nodiscard]] auto WriteAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> Result<void>
  {
    return file_.WriteAt(at, in);
  }

  [[nodiscard]] auto Reserve(const ByteCount size) noexcept -> Result<void>
  {
    return file_.Reserve(size);
  }

  [[nodiscard]] auto Flush(const Durability durability) noexcept -> Result<void>
  {
    return file_.Flush(durability);
  }

  //! Reads up to `out.size()` bytes at `at`, fewer only when the file ends
  //! first. `out` stays valid until the operation completes.
  [[nodiscard]] auto AsyncReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> ReadOperation
  {
    return { Target(), at, out };
  }

  //! Writes all of `in` at `at`, extending the file when it ends before.
  //! `in` stays valid until the operation completes.
  [[nodiscard]] auto AsyncWriteAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> WriteOperation
  {
    return { Target(), at, in };
  }

  //! The logical path the file was opened from.
  [[nodiscard]] auto Path() const noexcept -> const std::filesystem::path&
  {
    return file_.Path();
  }

  //! The OS handle, for code that hands the file to another OS API.
  [[nodiscard]] auto Handle() const noexcept -> NativeHandle
  {
    return file_.Handle();
  }

private:
  friend class IoDriver;

  AsyncFile(NativeFile file, detail::DriverState& driver,
    detail::BackendFileSlot slot) noexcept;

  auto Close() noexcept -> void;

  [[nodiscard]] auto Target() const noexcept -> detail::IoTarget
  {
    return {
      .driver = driver_,
      .handle = file_.Handle(),
      .slot = slot_,
    };
  }

  NativeFile file_;
  detail::DriverState* driver_ { nullptr };
  detail::BackendFileSlot slot_;
};

static_assert(AsyncWritableDevice<AsyncFile>);

} // namespace oxygen::serio
