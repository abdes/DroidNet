//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

//! Bounds the payload bytes Content's loads hold in flight.
/*!
 A resource load leases its payload's size before it allocates the buffer it
 reads the payload into, and returns the lease when its loader has taken the
 buffer. Loads that would exceed the budget wait:

 - **in arrival order, with no barging:** a load that would fit waits while
   an earlier one waits, so a large payload is never starved by small ones;
 - **oversized payloads alone:** a payload larger than the whole budget is
   served when nothing else is in flight;
 - **cancellation:** a cancelled waiter leaves the queue, and the waiters
   behind it are served as if it had never come.

 The budget lives on the loop thread that runs the loads; it is not
 thread-safe.
*/
class PayloadBudget {
public:
  //! Bytes leased from a budget, returned when the lease is destroyed.
  class Lease {
  public:
    Lease() noexcept = default;

    ~Lease() noexcept { Return(); }

    OXYGEN_MAKE_NON_COPYABLE(Lease)

    Lease(Lease&& other) noexcept
      : budget_(other.budget_)
      , bytes_(other.bytes_)
    {
      other.budget_ = nullptr;
      other.bytes_ = 0;
    }

    auto operator=(Lease&& other) noexcept -> Lease&
    {
      if (this != &other) {
        Return();
        budget_ = other.budget_;
        bytes_ = other.bytes_;
        other.budget_ = nullptr;
        other.bytes_ = 0;
      }
      return *this;
    }

    //! The bytes the lease holds.
    [[nodiscard]] auto Bytes() const noexcept -> serio::ByteCount
    {
      return serio::ByteCount { bytes_ };
    }

  private:
    friend class PayloadBudget;

    Lease(PayloadBudget& budget, const uint64_t bytes) noexcept
      : budget_(&budget)
      , bytes_(bytes)
    {
    }

    auto Return() noexcept -> void
    {
      if (budget_) {
        budget_->Release(bytes_);
        budget_ = nullptr;
        bytes_ = 0;
      }
    }

    observer_ptr<PayloadBudget> budget_ {};
    uint64_t bytes_ { 0 };
  };

  //! A budget of `capacity` bytes.
  OXGN_CNTT_API explicit PayloadBudget(serio::ByteCount capacity) noexcept;

  ~PayloadBudget() = default;

  OXYGEN_MAKE_NON_COPYABLE(PayloadBudget)
  OXYGEN_MAKE_NON_MOVABLE(PayloadBudget)

  //! Leases `bytes`, waiting until they fit.
  OXGN_CNTT_NDAPI auto Acquire(serio::ByteCount bytes) -> co::Co<Lease>;

  //! The bytes leased now.
  [[nodiscard]] auto InUse() const noexcept -> serio::ByteCount
  {
    return serio::ByteCount { in_use_ };
  }

  //! The loads waiting for bytes.
  [[nodiscard]] auto Waiting() const noexcept -> size_t
  {
    return queue_.size();
  }

  //! The budget's size.
  [[nodiscard]] auto Capacity() const noexcept -> serio::ByteCount
  {
    return serio::ByteCount { capacity_ };
  }

private:
  struct Waiter {
    uint64_t bytes { 0 };
    co::Event granted;
  };

  [[nodiscard]] auto Fits(uint64_t bytes) const noexcept -> bool;
  OXGN_CNTT_API auto Release(uint64_t bytes) noexcept -> void;
  //! Grants the waiters at the head of the queue that fit, in order.
  auto Grant() noexcept -> void;

  uint64_t capacity_;
  uint64_t in_use_ { 0 };
  std::deque<Waiter*> queue_;
};

} // namespace oxygen::content::internal
