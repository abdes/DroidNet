//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/OxCo/Detail/MuxBase.h"
#include "Oxygen/OxCo/Detail/MuxHelper.h"
#include "Oxygen/OxCo/Detail/Optional.h"
#include "Oxygen/OxCo/Detail/Result.h"

namespace oxygen::co::detail {

template <class Self, class Range>
class MuxRange : public MuxBase<Self> {
    using Item = decltype(*std::declval<Range>().begin());

public:
    // See note in MuxBase::await_cancel() regarding why we only can propagate
    // Abortable if the mux completes when its first awaitable does
    static constexpr auto IsAbortable() -> bool
    {
        return Self::kDoneOnFirstReady && Abortable<AwaitableType<Item>>;
    }
    static constexpr auto IsSkippable() -> bool
    {
        return Skippable<AwaitableType<Item>>;
    }

    [[nodiscard]] auto Size() const { return awaitables_.size(); }

    explicit MuxRange(Range&& range) // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    {
        awaitables_.reserve(range.size());
        for (auto& awaitable : range) {
            awaitables_.emplace_back(GetAwaitable(std::move(awaitable)));
        }
    }

    void await_set_executor(Executor* ex) noexcept
    {
        for (auto& awaitable : awaitables_) {
            awaitable.SetExecutor(ex);
        }
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        if (awaitables_.empty()) {
            return true;
        }
        size_t nReady = 0;
        bool allCanSkipKickoff = true;
        for (auto& awaitable : awaitables_) {
            if (awaitable.IsReady()) {
                ++nReady;
            } else if (!awaitable.IsSkippable()) {
                allCanSkipKickoff = false;
            }
        }
        return nReady >= this->self().MinReady() && allCanSkipKickoff;
    }

    auto await_suspend(Handle h) -> bool
    {
        bool ret = this->DoSuspend(h);
        for (auto& awaitable : awaitables_) {
            awaitable.Bind(*this);
        }
        for (auto& awaitable : awaitables_) {
            awaitable.Suspend();
        }
        return ret;
    }

    auto await_resume() &&
    {
        HandleResumeWithoutSuspend();
        this->ReRaise();
        std::vector<Optional<AwaitableReturnType<Item>>> ret;
        ret.reserve(awaitables_.size());
        for (auto& awaitable : awaitables_) {
            ret.emplace_back(std::move(awaitable).AsOptional());
        }
        return ret;
    }

    auto InternalCancel() noexcept -> bool
    {
        bool all_cancelled = true;
        for (auto& awaitable : awaitables_) {
            all_cancelled &= awaitable.Cancel();
        }
        return all_cancelled;
    }

    auto await_must_resume() const noexcept
    {
        bool anyMustResume = false;
        for (auto& awaitable : awaitables_) {
            anyMustResume |= awaitable.MustResume();
        }

        // See note in MuxTuple::await_must_resume()
        if constexpr (Skippable<Self> && Abortable<Self>) {
            DCHECK_F(!anyMustResume);
            return std::false_type {};
        } else {
            return anyMustResume;
        }
    }

    static constexpr bool doneOnFirstReady = false;

protected:
    auto GetAwaitables() -> auto& { return awaitables_; }
    auto GetAwaitables() const -> const auto& { return awaitables_; }

    void HandleResumeWithoutSuspend()
    {
        if (!awaitables_.empty() && !awaitables_[0].IsBound()) {
            // We skipped await_suspend because all awaitables were ready or
            // sync-cancellable. (All the MuxHelpers have bind() called at the
            // same time; we check the first one for convenience only.)
            [[maybe_unused]] auto _ = this->DoSuspend(std::noop_coroutine());
            for (auto& awaitable : awaitables_) {
                awaitable.Bind(*static_cast<Self*>(this));
            }
            for (auto& awaitable : awaitables_) {
                awaitable.ReportImmediateResult();
            }
        }
    }

private:
    std::vector<MuxHelper<MuxRange, AwaitableType<Item>>> awaitables_;
};

template <class Range>
class AnyOfRange : public MuxRange<AnyOfRange<Range>, Range> {
    using Item = decltype(*std::declval<Range>().begin());
    static_assert(Cancellable<AwaitableType<Item>>,
        "anyOf() makes no sense for non-cancellable awaitables");

public:
    using AnyOfRange::MuxRange::MuxRange;

    [[nodiscard]] auto MinReady() const noexcept { return std::min<size_t>(1, this->Size()); }
    static constexpr bool kDoneOnFirstReady = true;
};

template <class Range>
class MostOfRange : public MuxRange<MostOfRange<Range>, Range> {
public:
    using MostOfRange::MuxRange::MuxRange;
    [[nodiscard]] auto MinReady() const noexcept -> size_t { return this->size(); }
};

template <class Range>
class AllOfRange : public MuxRange<AllOfRange<Range>, Range> {
    using Item = decltype(*std::declval<Range>().begin());

public:
    using AllOfRange::MuxRange::MuxRange;
    [[nodiscard]] auto MinReady() const noexcept { return this->Size(); }

    [[nodiscard]] auto await_must_resume() const noexcept -> bool
    {
        bool allMustResume = (this->Size() > 0);
        for (auto& awaitable : this->GetAwaitables()) {
            allMustResume &= awaitable.MustResume();
        }
        return this->HasException() || allMustResume;
    }

    auto await_resume() && -> std::vector<AwaitableReturnType<Item>>
    {
        this->HandleResumeWithoutSuspend();
        this->ReRaise();
        std::vector<AwaitableReturnType<Item>> ret;
        ret.reserve(this->GetAwaitables().size());
        for (auto& awaitable : this->GetAwaitables()) {
            ret.emplace_back(std::move(awaitable).Result());
        }
        return ret;
    }
};

} // namespace oxygen::co::detail
