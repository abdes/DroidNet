//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/ParkingLotImpl.h"

namespace oxygen::co {

//! A wait queue, entered when its `Awaitable` is awaited with `co_await`.
/*!
 To enter `co_await parking_lot.Park();`. Once parked, the coroutine is suspended
 until explicitly resumed by calling `UnParkOne()` or `UnParkAll()`.
*/
class ParkingLot : public detail::ParkingLotImpl<ParkingLot> {
public:
    using Base = ParkingLotImpl;
    class Awaitable final : public Parked {
    public:
        using Parked::Parked;
        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        [[nodiscard]] auto await_ready() const noexcept { return false; }
        void await_suspend(const detail::Handle h) { this->DoSuspend(h); }
        void await_resume() { }
        // ReSharper restore CppMemberFunctionMayBeStatic
    };

    //! Returns an `Awaitable` which, when `co_await`'ed, suspends the caller
    //! until any of `UnPark()` or `UnParkAll()` methods are called.
    [[nodiscard]] auto Park()
    {
        return Awaitable(*this);
    }

    using Base::Empty;
    using Base::ParkedCount;
    using Base::UnParkAll;
    using Base::UnParkOne;
};

static_assert(Awaitable<ParkingLot::Awaitable>);

} // namespace oxygen::co
