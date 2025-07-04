//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "IntrusiveList.h"
#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Coroutine.h>

namespace oxygen::co::detail {

template <typename Self> class ParkingLotImpl {
public:
  virtual ~ParkingLotImpl() = default;

protected:
  class Parked : public IntrusiveListItem<Parked> {
  public:
    explicit Parked(Self& object)
      : object_(object)
    {
    }

    auto await_cancel(Handle) noexcept
    {
      this->Unlink();
      handle_ = std::noop_coroutine();
      return std::true_type {};
    }

    friend class ParkingLotImpl;

  protected:
    [[nodiscard]] auto Object() -> Self& { return object_; }
    [[nodiscard]] auto Object() const -> const Self& { return object_; }

    void DoSuspend(const Handle h)
    {
      handle_ = h;
      object_.parked_.PushBack(*this);
    }

    void UnPark()
    {
      this->Unlink();
      std::exchange(handle_, std::noop_coroutine()).resume();
    }

  private:
    Self& object_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    Handle handle_;
  };

  OXYGEN_MAKE_NON_COPYABLE(ParkingLotImpl)
  OXYGEN_DEFAULT_MOVABLE(ParkingLotImpl)

  //! Return a pointer to the awaiter whose task `UnParkOne()` would wake, or
  //! `nullptr` if there are no waiters currently. You can use its `UnPark()`
  //! method to wake it and remove it from the list of waiters.
  Parked* Peek()
  {
    if (!parked_.empty()) {
      return &parked_.front();
    }
    return nullptr;
  }

  //! Wake the oldest waiter, removing it from the list of waiters.
  void UnParkOne()
  {
    if (!parked_.Empty()) {
      parked_.Front().UnPark();
    }
  }

  //! Wake all waiters that were waiting when the call to `UnParkAll()` began.
  void UnParkAll()
  {
    auto parked = std::move(parked_);
    while (!parked.Empty()) {
      parked.Front().UnPark();
    }
  }

  //! Returns true if no tasks are waiting, which implies `Front()` and
  //! `Back()` will return `nullptr` and `UnParkOne()` and `UnParkAll()` are
  //! no-ops.
  // ReSharper disable once CppHiddenFunction
  [[nodiscard]] auto Empty() const { return parked_.Empty(); }

  //! Returns the number of tasks waiting.
  /*!
   This is an O(n) operation, which counts the number of tasks parked in the
   parking lot.
   */
  [[nodiscard]] auto ParkedCount() const
  {
    return std::ranges::distance(parked_);
  }

private:
  // CRTP: Constructors are private and the derived class is a friend.
  ParkingLotImpl() = default;
  friend Self;

  // We need to collect the parked tasks using a collection that does not
  // require the type to be fully defined. A linked list is a good choice.
  IntrusiveList<Parked> parked_;
};

} // namespace oxygen::co::detail
