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
    using Awaitable = decltype(*std::declval<Range>().begin());
    using Helper = MuxHelper<MuxRange<Self, Range>, Awaitable>;

    struct ItemWithoutAwaitable {
        Helper helper;
        explicit ItemWithoutAwaitable(Awaitable&& awaitable)
            : helper(std::forward<Awaitable>(awaitable))
        {
        }
    };

    struct ItemWithAwaitable {
        Awaitable awaitable;
        Helper helper;
        explicit ItemWithAwaitable(Awaitable&& aw)
            : awaitable(std::forward<Awaitable>(aw))
            , helper(std::forward<Awaitable>(awaitable))
        {
        }
    };

protected:
    using Item = std::conditional_t<std::is_reference_v<Awaitable>,
        ItemWithoutAwaitable,
        ItemWithAwaitable>;

public:
    // See note in MuxBase::await_cancel() regarding why we only can propagate
    // Abortable if the mux completes when its first awaitable does
    static constexpr auto IsAbortable() -> bool
    {
        return Self::kDoneOnFirstReady && Abortable<AwaiterType<Awaitable>>;
    }
    static constexpr auto IsSkippable() -> bool
    {
        return Skippable<AwaiterType<Awaitable>>;
    }

    [[nodiscard]] auto Size() const { return count_; }

    explicit MuxRange(Range&& range)
    {
        items_ = static_cast<Item*>(operator new(
            sizeof(Item) * range.size(), std::align_val_t { alignof(Item) }));

        Item* p = items_;
        try {
            for (Awaitable&& awaitable : range) {
                new (p) Item(std::forward<Awaitable>(awaitable));
                ++p;
            }
        } catch (...) {
            while (p != items_) {
                (--p)->~Item();
            }
            operator delete(items_, std::align_val_t { alignof(Item) });
            throw;
        }

        count_ = p - items_;
    }

    ~MuxRange()
    {
        for (Item* p = items_ + count_; p != items_;) {
            (--p)->~Item();
        }
        operator delete(items_, std::align_val_t { alignof(Item) });
    }

    MuxRange(MuxRange&&) = delete;
    MuxRange(const MuxRange&) = delete;

    void await_set_executor(Executor* ex) noexcept
    {
        for (auto& item : GetItems()) {
            item.helper.SetExecutor(ex);
        }
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
        if (count_ == 0) {
            return true;
        }
        size_t n_ready = 0;
        bool all_can_skip_kickoff = true;
        for (auto& item : GetItems()) {
            if (item.helper.IsReady()) {
                ++n_ready;
            } else if (!item.helper.IsSkippable()) {
                all_can_skip_kickoff = false;
            }
        }
        return n_ready >= this->self().MinReady() && all_can_skip_kickoff;
    }

    auto await_suspend(Handle h) -> bool
    {
        const bool ret = this->DoSuspend(h);
        for (auto& item : GetItems()) {
            item.helper.Bind(*this);
        }
        for (auto& item : GetItems()) {
            item.helper.Suspend();
        }
        return ret;
    }

    auto await_resume() &&
    {
        HandleResumeWithoutSuspend();
        this->ReRaise();
        std::vector<Optional<AwaitableReturnType<Awaitable>>> ret;
        ret.reserve(count_);
        for (auto& item : GetItems()) {
            ret.emplace_back(std::move(item.helper).AsOptional());
        }
        return ret;
    }

    auto InternalCancel() noexcept -> bool
    {
        bool all_cancelled = true;
        for (auto& item : GetItems()) {
            all_cancelled &= item.helper.Cancel();
        }
        return all_cancelled;
    }

    auto await_must_resume() const noexcept
    {
        bool anyMustResume = false;
        for (auto& item : GetItems()) {
            anyMustResume |= item.helper.MustResume();
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
    auto GetItems() { return std::span(items_, count_); }
    auto GetItems() const { return std::span(items_, count_); }

    void HandleResumeWithoutSuspend()
    {
        if (count_ != 0 && !items_[0].helper.IsBound()) {
            // We skipped await_suspend because all awaitables were ready or
            // sync-cancellable. (All the MuxHelpers have bind() called at the
            // same time; we check the first one for convenience only.)
            [[maybe_unused]] auto _ = this->DoSuspend(std::noop_coroutine());
            for (auto& item : GetItems()) {
                item.helper.Bind(*static_cast<Self*>(this));
            }
            for (auto& item : GetItems()) {
                item.helper.ReportImmediateResult();
            }
        }
    }

private:
    Item* items_;
    uint64_t count_;
};

template <class Range>
class AnyOfRange : public MuxRange<AnyOfRange<Range>, Range> {
    using Item = decltype(*std::declval<Range>().begin());
    static_assert(Cancellable<AwaiterType<Item>>,
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
    using Awaitable = decltype(*std::declval<Range>().begin());

public:
    using AllOfRange::MuxRange::MuxRange;
    [[nodiscard]] auto MinReady() const noexcept { return this->Size(); }

    [[nodiscard]] auto await_must_resume() const noexcept -> bool
    {
        bool all_must_resume = this->Size() > 0;
        for (auto& item : this->GetItems()) {
            all_must_resume &= item.helper.MustResume();
        }
        return this->HasException() || all_must_resume;
    }

    auto await_resume() && -> std::vector<AwaitableReturnType<Awaitable>>
    {
        this->HandleResumeWithoutSuspend();
        this->ReRaise();
        std::vector<AwaitableReturnType<Awaitable>> ret;
        ret.reserve(this->GetItems().size());
        for (auto& item : this->GetItems()) {
            ret.emplace_back(std::move(item.helper).Result());
        }
        return ret;
    }
};

} // namespace oxygen::co::detail
