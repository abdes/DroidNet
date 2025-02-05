//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Concepts/Awaitable.h"
#include "Oxygen/OxCo/Detail/ParkingLotImpl.h"

namespace oxygen::co {

/// A semaphore, which can also be used to implement a lock.
///
/// The semaphore maintains an internal counter which logically tracks
/// a count of resources available to hand out; it is initially 1 or
/// can be overridden by passing an argument to the constructor.
/// `acquire()` waits for this counter to be at least 1, then decrements it.
/// `release()` increments the counter. `lock()` returns an RAII guard that
/// wraps `acquire()` and `release()`.
class Semaphore : public detail::ParkingLotImpl<Semaphore> {
public:
    template <class RetVal>
    class Awaitable;
    class LockGuard;

    explicit Semaphore(const size_t initial = 1)
        : value_(initial)
    {
    }

    [[nodiscard]] auto Value() const noexcept -> size_t { return value_; }

    /// An awaitable that decrements the semaphore, suspending
    /// the caller if it is currently zero.
    [[nodiscard]] auto Acquire() -> co::Awaitable<void> auto;

    /// Increments the semaphore, waking one suspended task (if any).
    void Release();

    /// RAII-style decrement, returning guard object which
    /// will increment the semaphore back upon going out of scope
    [[nodiscard]] auto Lock() -> co::Awaitable<LockGuard> auto;

private:
    size_t value_;
};

//
// Implementation
//

class [[nodiscard]] Semaphore::LockGuard {
public:
    LockGuard() = default;

    ~LockGuard()
    {
        if (sem_ != nullptr) {
            sem_->Release();
        }
    }

    LockGuard(LockGuard&& lk) noexcept
        : sem_(std::exchange(lk.sem_, nullptr))
    {
    }
    auto operator=(LockGuard&& lk) noexcept -> LockGuard& = delete;

    LockGuard(const LockGuard& lk) = delete;
    auto operator=(LockGuard lk) noexcept -> LockGuard&
    {
        std::swap(sem_, lk.sem_);
        return *this;
    }

private:
    explicit LockGuard(Semaphore& sem)
        : sem_(&sem)
    {
    }
    friend class Awaitable<LockGuard>;

    Semaphore* sem_ = nullptr;
};

template <class RetVal>
class Semaphore::Awaitable final : public Parked {
public:
    using Parked::Parked;
    [[nodiscard]] auto await_ready() const noexcept -> bool { return this->Object().Value() > 0; }

    void await_suspend(const detail::Handle h) { this->DoSuspend(h); }

    auto await_resume()
    {
        --this->Object().value_;
        if constexpr (!std::is_same_v<RetVal, void>) {
            return RetVal(this->Object());
        }
    }
};

inline auto Semaphore::Acquire() -> co::Awaitable<void> auto
{
    return Awaitable<void>(*this);
}

inline auto Semaphore::Lock() -> co::Awaitable<LockGuard> auto
{
    return Awaitable<LockGuard>(*this);
}

inline void Semaphore::Release()
{
    ++value_;
    UnParkOne();
}

} // namespace oxygen::co
