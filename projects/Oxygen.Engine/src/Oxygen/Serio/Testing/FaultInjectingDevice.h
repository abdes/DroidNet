//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio::testing {

//! The device operations a `FaultInjectingDevice` can fail.
enum class FaultOperation : uint8_t {
  kRead,
  kWrite,
  kReserve,
  kFlush,
};

//! A device over a device that fails the operations a test schedules and
//! counts the calls it receives.
/*!
 It satisfies the device concepts its inner device satisfies, except
 `ContiguousDevice`: it never exposes a view, so streams and parsers over it
 always take the copying path, where the faults are.

 - `FailOn` fails one call of an operation with a chosen error;
 - `LimitReads` makes every read short;
 - `LimitCapacity` makes writes that end past a capacity fail with
   `no_space_on_device`, which compares equal to `kNoSpace` (a full disk);
 - `CallCount` reports how many calls of an operation reached the device, so
   a test checks how a buffered stream batches its IO.

 Schedule faults before the device is used concurrently.

 @tparam D The inner device type.
*/
template <ReadableDevice D> class FaultInjectingDevice {
public:
  explicit FaultInjectingDevice(D device)
    : device_(std::move(device))
    , schedule_(std::make_unique<Schedule>())
  {
  }

  //! Fails the `nth` call of `operation` from now (zero is the next) with
  //! `error`.
  auto FailOn(const FaultOperation operation, const uint32_t nth,
    const std::error_code error) noexcept -> void
  {
    const auto index = static_cast<size_t>(operation);
    schedule_->fail_at.at(index)
      = schedule_->calls.at(index).load(std::memory_order_relaxed) + nth;
    schedule_->errors.at(index) = error;
  }

  //! Every read returns at most `max` bytes (at least one).
  auto LimitReads(const ByteCount max) noexcept -> void
  {
    schedule_->max_read = (std::max)(max.get(), uint64_t { 1 });
  }

  //! Writes ending past `capacity` fail with `no_space_on_device`.
  auto LimitCapacity(const ByteCount capacity) noexcept -> void
  {
    schedule_->capacity = capacity.get();
  }

  //! The number of calls of `operation` so far, failed ones included.
  [[nodiscard]] auto CallCount(const FaultOperation operation) const noexcept
    -> uint32_t
  {
    return schedule_->calls.at(static_cast<size_t>(operation))
      .load(std::memory_order_relaxed);
  }

  //! The inner device.
  [[nodiscard]] auto Inner() const noexcept -> const D& { return device_; }

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return device_.Size();
  }

  [[nodiscard]] auto ReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    if (const auto error = Fault(FaultOperation::kRead)) {
      return ::oxygen::Err(error);
    }
    const auto count = static_cast<size_t>(
      (std::min)(uint64_t { out.size() }, schedule_->max_read));
    return device_.ReadAt(at, out.first(count));
  }

  [[nodiscard]] auto WriteAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> Result<void>
    requires WritableDevice<D>
  {
    if (const auto error = Fault(FaultOperation::kWrite)) {
      return ::oxygen::Err(error);
    }
    if (in.size() > schedule_->capacity
      || at.get() > schedule_->capacity - in.size()) {
      return ::oxygen::Err(std::make_error_code(std::errc::no_space_on_device));
    }
    return device_.WriteAt(at, in);
  }

  [[nodiscard]] auto Reserve(const ByteCount size) noexcept -> Result<void>
    requires WritableDevice<D>
  {
    if (const auto error = Fault(FaultOperation::kReserve)) {
      return ::oxygen::Err(error);
    }
    if (size.get() > schedule_->capacity) {
      return ::oxygen::Err(std::make_error_code(std::errc::no_space_on_device));
    }
    return device_.Reserve(size);
  }

  [[nodiscard]] auto Flush(const Durability durability) noexcept -> Result<void>
    requires WritableDevice<D>
  {
    if (const auto error = Fault(FaultOperation::kFlush)) {
      return ::oxygen::Err(error);
    }
    return device_.Flush(durability);
  }

private:
  static constexpr size_t kOperations = 4;
  static constexpr auto kNever = std::numeric_limits<uint32_t>::max();

  //! Held on the heap so the device stays movable while the counters are
  //! atomic, as `const` reads may run concurrently.
  struct Schedule {
    std::array<std::atomic<uint32_t>, kOperations> calls {};
    std::array<uint32_t, kOperations> fail_at {
      kNever,
      kNever,
      kNever,
      kNever,
    };
    std::array<std::error_code, kOperations> errors {};
    uint64_t max_read { std::numeric_limits<uint64_t>::max() };
    uint64_t capacity { std::numeric_limits<uint64_t>::max() };
  };

  //! Counts a call of `operation`; the scheduled error when it is due.
  [[nodiscard]] auto Fault(const FaultOperation operation) const noexcept
    -> std::error_code
  {
    const auto index = static_cast<size_t>(operation);
    const auto call
      = schedule_->calls.at(index).fetch_add(1, std::memory_order_relaxed);
    if (call == schedule_->fail_at.at(index)) {
      return schedule_->errors.at(index);
    }
    return {};
  }

  D device_;
  std::unique_ptr<Schedule> schedule_;
};

} // namespace oxygen::serio::testing
