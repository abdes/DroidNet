//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/PayloadBudget.h>

namespace oxygen::content::internal {

PayloadBudget::PayloadBudget(const serio::ByteCount capacity) noexcept
  : capacity_(capacity.get())
{
}

auto PayloadBudget::Acquire(const serio::ByteCount bytes) -> co::Co<Lease>
{
  const auto size = bytes.get();
  if (queue_.empty() && Fits(size)) {
    in_use_ += size;
    co_return Lease(*this, size);
  }

  auto waiter = Waiter { .bytes = size };
  queue_.push_back(&waiter);
  // A cancelled waiter leaves the queue. A granted one is resumed at once,
  // inside the grant (`co::Event::Trigger` resumes its waiters), so it takes
  // its bytes before anything can cancel it.
  const auto leave = Finally([this, &waiter]() noexcept -> void {
    if (!waiter.granted.Triggered()) {
      std::erase(queue_, &waiter);
      Grant();
    }
  });
  co_await waiter.granted;
  co_return Lease(*this, size);
}

auto PayloadBudget::Fits(const uint64_t bytes) const noexcept -> bool
{
  // A payload larger than the budget is served alone.
  return in_use_ == 0 || bytes <= capacity_ - (std::min)(in_use_, capacity_);
}

auto PayloadBudget::Release(const uint64_t bytes) noexcept -> void
{
  DCHECK_LE_F(bytes, in_use_);
  in_use_ -= bytes;
  Grant();
}

auto PayloadBudget::Grant() noexcept -> void
{
  while (!queue_.empty() && Fits(queue_.front()->bytes)) {
    auto* waiter = queue_.front();
    queue_.pop_front();
    in_use_ += waiter->bytes;
    waiter->granted.Trigger();
  }
}

} // namespace oxygen::content::internal
