//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>

#include <asio/cancellation_signal.hpp>
#include <asio/executor.hpp>
#include <asio/high_resolution_timer.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Executor.h"

namespace oxygen::co {

namespace detail {
    template <class Executor, bool ThrowOnError>
    struct asio_awaitable_t {
        constexpr asio_awaitable_t() = default;
    };
} // namespace detail

//! An `ASIO` completion token, suitable for passing into any `asio::async_*()`
//! function, which would convert it into corral C++20 awaitable.
/*!
 Requires the first argument of the completion signature to be `error_code`, and
 throws `system_error` exception if called with a nontrivial error code.

 \code{cpp}
    asio::deadline_timer t(io);
    co::Awaitable<void> auto aw = t.async_wait(co::asio_awaitable);
    co_await aw;
 \endcode
*/
// ReSharper disable once CppInconsistentNaming (keep name consistent with asio)
static constexpr detail::asio_awaitable_t<asio::executor, true>
    asio_awaitable;

//! Same as `asio_awaitable`, but does not throw any exceptions, instead
/// pre-pending its `co_return`ed value with `error_code`.
/*!
 \code{cpp}
    co::Awaitable<asio::error_code> auto aw =
        t.async_wait(co::asio_nothrow_awaitable);
    auto ec = co_await aw;
 \endcode
*/
// ReSharper disable once CppInconsistentNaming (keep name consistent with asio)
static constexpr detail::asio_awaitable_t<asio::executor, false>
    asio_nothrow_awaitable;

// -----------------------------------------------------------------------------
// Implementation

namespace detail {

    template <class Self, bool ThrowOnError, class... Ret>
    class AsioAwaitableBase {
        using Err = asio::error_code;

    protected:
        struct DoneCallBack {
            AsioAwaitableBase* aw { nullptr };

            void operator()(Err err, Ret... ret) const
            {
                aw->Done(err, std::forward<Ret>(ret)...);
            }

            using cancellation_slot_type = asio::cancellation_slot;
            [[nodiscard]] auto get_cancellation_slot() const -> cancellation_slot_type
            {
                return aw->CancelSignal().slot();
            }
        };

    public:
        // ReSharper disable CppMemberFunctionMayBeStatic
        [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }

        void await_suspend(const Handle h)
        {
            new (&cancel_sig_) asio::cancellation_signal();
            parent_ = h;
            done_cb_ = DoneCallBack { this };
            static_cast<Self*>(this)->KickOff();
        }

        auto await_resume() noexcept(!ThrowOnError)
        {
            if constexpr (ThrowOnError) {
                if (ec_) {
                    throw asio::system_error(ec_);
                }

                if constexpr (sizeof...(Ret) == 0) {
                    // return
                } else if constexpr (sizeof...(Ret) == 1) {
                    return std::move(std::get<0>(*ret_));
                } else {
                    return std::move(*ret_);
                }
            } else {
                if constexpr (sizeof...(Ret) == 0) {
                    return ec_;
                } else {
                    return std::tuple_cat(std::make_tuple(ec_), std::move(*ret_));
                }
            }
        }

        [[nodiscard]] auto await_cancel(Handle /*h*/) noexcept -> bool
        {
            CancelSignal().emit(asio::cancellation_type::all);
            return false;
        }

        [[nodiscard]] auto await_must_resume() const noexcept -> bool
        {
            return ec_ != asio::error::operation_aborted;
        }
        // ReSharper restore CppMemberFunctionMayBeStatic

    protected:
        [[nodiscard]] auto GetDoneCallBack() -> auto& { return done_cb_; }

    private:
        // CRTP: constructor is private, and derived is friend so it can only
        // cbe constructed from the derived class.
        AsioAwaitableBase() = default;
        friend Self;

        [[nodiscard]] auto CancelSignal() -> asio::cancellation_signal&
        {
            return *std::launder(
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast) - see comment below
                reinterpret_cast<asio::cancellation_signal*>(&cancel_sig_));
        }

        void Done(const Err ec, Ret... ret)
        {
            ec_ = ec;
            ret_.emplace(std::forward<Ret>(ret)...);
            CancelSignal().~cancellation_signal();
            std::exchange(parent_, std::noop_coroutine())();
        }

        template <class T>
        using StorageFor = std::aligned_storage_t<sizeof(T), alignof(T)>;

        Err ec_;
        std::optional<std::tuple<Ret...>> ret_;
        Handle parent_;
        DoneCallBack done_cb_;

        // `cancellation_signal` is non-moveable, while `AsioAwaitable` needs to
        // be moveable, so we use `aligned_storage` to store it.
        //
        // `cancellation_signal` lifetime spans from `await_suspend(`) to
        // `Done()`; `AsioAwaitable` is guaranteed not to get moved during that
        // time window.
        StorageFor<asio::cancellation_signal> cancel_sig_ {};
    };

    template <bool ThrowOnError, class Init, class Args, class... Ret>
    class AsioAwaitable
        : public AsioAwaitableBase<AsioAwaitable<ThrowOnError, Init, Args, Ret...>,
              ThrowOnError,
              Ret...> {
        using Base = AsioAwaitableBase<AsioAwaitable, ThrowOnError, Ret...>;
        friend Base;

    public:
        template <class... Ts>
        explicit AsioAwaitable(Init&& init, Ts&&... args) // NOLINT(*-rvalue-reference-param-not-moved)
            : init_(std::forward<Init>(init)) // perfect forwarding
            , args_(std::forward<Ts>(args)...)
        {
        }

    private /*methods*/:
        void KickOff()
        {
            auto impl = [this]<class... A>(A&&... args) {
                using Sig = void(asio::error_code, Ret...);
                asio::async_initiate<typename Base::DoneCallBack, Sig>(
                    std::forward<Init>(init_), this->GetDoneCallBack(),
                    std::forward<A>(args)...);
            };
            std::apply(impl, args_);
        }

        Init init_;
        [[no_unique_address]] Args args_;
        template <bool, class...>
        friend class TypeErasedAsioAwaitable;
    };

    /// AsioAwaitable is parametrized by its initiation object (as it needs
    /// to store it). `asio::async_result<>` does not have initiation among
    /// its type parameter list, yet needs to export something under dependent
    /// name `return_type`, which is used for asio-related functions still
    /// having an explicit return type (like Boost.Beast).
    ///
    /// To accommodate that, we have this class, which stores a type-erased
    /// initiation object, and is constructible from `AsioAwaitable`.
    template <bool ThrowOnError, class... Ret>
    class TypeErasedAsioAwaitable
        : public AsioAwaitableBase<TypeErasedAsioAwaitable<ThrowOnError, Ret...>,
              ThrowOnError,
              Ret...> {
        using Base = AsioAwaitableBase<TypeErasedAsioAwaitable, ThrowOnError, Ret...>;
        friend Base;

        struct InitFn {
            virtual ~InitFn() = default;
            virtual void KickOff(typename Base::DoneCallBack&) = 0;

            OXYGEN_DEFAULT_COPYABLE(InitFn)
            OXYGEN_DEFAULT_MOVABLE(InitFn)
        };
        template <class Init, class Args>
        struct InitFnImpl final : InitFn {
            Init init;
            Args args;

            InitFnImpl(Init&& init, Args&& args) // NOLINT(*-rvalue-reference-param-not-moved)
                : init(std::forward<Init>(init)) // Perfect forwarding
                , args(std::move(args))
            {
            }
            void KickOff(typename Base::DoneCB& done_cb) override
            {
                auto impl = [this, &done_cb]<class... A>(A&&... the_args) {
                    asio::async_initiate<typename Base::DoneCB,
                        void(asio::error_code,
                            Ret...)>(
                        std::forward<Init>(init), done_cb,
                        std::forward<A>(the_args)...);
                };
                std::apply(impl, args);
            }
        };

        std::unique_ptr<InitFn> init_fn_;

    public:
        template <class Init, class Args>
        explicit(false) TypeErasedAsioAwaitable(
            AsioAwaitable<ThrowOnError, Init, Args, Ret...>&& rhs) // NOLINT(*-rvalue-reference-param-not-moved)
            : init_fn_(std::make_unique<InitFnImpl<Init, Args>>(
                  std::move(rhs.init_), std::move(rhs.args_)))
        {
        }

    private:
        void KickOff() { init_fn_->KickOff(this->doneCB()); }
    };

} // namespace detail

//! A specialization of the `oxygen::co::EventLoopTraits` for
//! `asio::io_context`.
/*!
 Note that this is useful when the event loop is totally delegated to `asio`. An
 alternate possible approach is to define a custom event loop that calls
 `poll()` on the `io_context` to handle events on demand.
*/
template <>
struct EventLoopTraits<asio::io_context> {
    static auto EventLoopId(asio::io_context& io) -> EventLoopID
    {
        return EventLoopID(&io);
    }

    static void Run(asio::io_context& io)
    {
        io.run();
        io.reset();
    }

    static void Stop(asio::io_context& io) { io.stop(); }
};

} // namespace oxygen::co

template <class Executor, bool ThrowOnError, class X, class... Ret>
class asio::async_result<oxygen::co::detail::asio_awaitable_t<Executor, ThrowOnError>,
    X(asio::error_code, Ret...)> {
public:
    using return_type = oxygen::co::detail::TypeErasedAsioAwaitable<ThrowOnError, Ret...>;

    //! Use `AsioAwaitable` here, so `asio::async_*()` functions which don't use
    //! `return_type` and instead have `auto` for their return types will do
    //! without type erase.
    template <class Init, class... Args>
    static auto initiate(
        Init&& init,
        oxygen::co::detail::asio_awaitable_t<Executor, ThrowOnError> /*unused*/,
        Args... args)
    {
        return oxygen::co::detail::AsioAwaitable<ThrowOnError, Init,
            std::tuple<Args...>, Ret...>(
            std::forward<Init>(init), std::move(args)...);
    }
};

namespace oxygen::co {
namespace detail {
    //! A helper class for `corral::SleepFor()`.
    class Timer {
    public:
        template <class R, class P>
        Timer(asio::io_context& io, std::chrono::duration<R, P> delay)
            : timer_(io)
        {
            timer_.expires_from_now(delay);
        }

        auto operator co_await() -> Awaitable<void> auto
        {
            return timer_.async_wait(asio_awaitable);
        }

    private:
        asio::high_resolution_timer timer_;
    };
} // namespace detail

//! A utility function, returning an awaitable suspending the caller for a
//! specified duration. Suitable for use with `AnyOf()` etc.
template <class R, class P>
auto SleepFor(asio::io_context& io, std::chrono::duration<R, P> delay)
{
    return detail::Timer(io, delay);
}

} // namespace oxygen::co
