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

    template <bool ThrowOnError, class Init, class Args, class... Ret>
    class AsioAwaitableFactory;
    template <bool ThrowOnError, class... Ret>
    class TypeErasedAsioAwaitableFactory;

    template <class... Ret>
    class AsioAwaitableBase {
    protected:
        using Err = asio::error_code;
        struct DoneCB {
            AsioAwaitableBase* aw_;

            void operator()(Err err, Ret... ret) const
            {
                aw_->done(err, std::forward<Ret>(ret)...);
            }

            using cancellation_slot_type = asio::cancellation_slot;
            cancellation_slot_type get_cancellation_slot() const
            {
                return aw_->cancelSig_.slot();
            }
        };

        void done(Err ec, Ret... ret)
        {
            ec_ = ec;
            ret_.emplace(std::forward<Ret>(ret)...);
            std::exchange(parent_, std::noop_coroutine())();
        }

    protected:
        Err ec_;
        std::optional<std::tuple<Ret...>> ret_;
        Handle parent_;
        DoneCB doneCB_;
        asio::cancellation_signal cancelSig_;

        template <bool, class, class, class...>
        friend class AsioAwaitableFactory;
        template <bool, class...>
        friend class TypeErasedAsioAwaitableFactory;
    };

    template <class InitFn, bool ThrowOnError, class... Ret>
    class AsioAwaitable : private AsioAwaitableBase<Ret...> {
        using Err = asio::error_code;

    public:
        explicit AsioAwaitable(InitFn&& initFn)
            : initFn_(std::forward<InitFn>(initFn))
        {
        }

        AsioAwaitable(AsioAwaitable&&) = delete;

        bool await_ready() const noexcept { return false; }

        void await_suspend(Handle h)
        {
            this->parent_ = h;
            this->doneCB_ = typename AsioAwaitableBase<Ret...>::DoneCB { this };
            this->initFn_(this->doneCB_);
        }

        auto await_resume() noexcept(!ThrowOnError)
        {
            if constexpr (ThrowOnError) {
                if (this->ec_) {
                    throw asio::system_error(this->ec_);
                }

                if constexpr (sizeof...(Ret) == 0) {
                    return;
                } else if constexpr (sizeof...(Ret) == 1) {
                    return std::move(std::get<0>(*this->ret_));
                } else {
                    return std::move(*this->ret_);
                }
            } else {
                if constexpr (sizeof...(Ret) == 0) {
                    return this->ec_;
                } else {
                    return std::tuple_cat(std::make_tuple(this->ec_),
                        std::move(*this->ret_));
                }
            }
        }

        bool await_cancel(Handle) noexcept
        {
            this->cancelSig_.emit(asio::cancellation_type::all);
            return false;
        }

        bool await_must_resume() const noexcept
        {
            return this->ec_ != asio::error::operation_aborted;
        }

    private:
        [[no_unique_address]] InitFn initFn_;
    };

    template <bool ThrowOnError, class Init, class Args, class... Ret>
    class AsioAwaitableFactory {
        using DoneCB = typename AsioAwaitableBase<Ret...>::DoneCB;

        struct InitFn {
            Init init_;
            [[no_unique_address]] Args args_;

            template <class... Ts>
            explicit InitFn(Init&& init, Ts&&... ts)
                : init_(std::forward<Init>(init))
                , args_(std::forward<Ts>(ts)...)
            {
            }

            void operator()(DoneCB& doneCB)
            {
                auto impl = [this, &doneCB]<class... A>(A&&... args) {
                    using Sig = void(asio::error_code, Ret...);
                    asio::async_initiate<DoneCB, Sig>(
                        std::move(init_), doneCB, std::forward<A>(args)...);
                };
                std::apply(impl, args_);
            }
        };

    public:
        template <class... Ts>
        explicit AsioAwaitableFactory(Init&& init, Ts&&... args)
            : initFn_(std::forward<Init>(init), std::forward<Ts>(args)...)
        {
        }

        DirectAwaitable auto operator co_await() &&
        {
            return AsioAwaitable<InitFn, ThrowOnError, Ret...>(
                std::forward<InitFn>(initFn_));
        }

    private:
        InitFn initFn_;

        friend TypeErasedAsioAwaitableFactory<ThrowOnError, Ret...>;
    };

    /// AsioAwaitableFactory is parametrized by its initiation object (as it needs
    /// to store it). `asio::async_result<>` does not have initiation among
    /// its type parameter list, yet needs to export something under dependent
    /// name `return_type`, which is used for asio-related functions still
    /// having an explicit return type (like Boost.Beast).
    ///
    /// To accommodate that, we have this class, which stores a type-erased
    /// initiation object, and is constructible from `AsioAwaitableFactory`.
    template <bool ThrowOnError, class... Ret>
    class TypeErasedAsioAwaitableFactory {
        using DoneCB = typename AsioAwaitableBase<Ret...>::DoneCB;

        struct InitFnBase {
            virtual ~InitFnBase() = default;
            virtual void operator()(DoneCB&) = 0;
        };

        template <class Impl>
        struct InitFnImpl : InitFnBase {
            explicit InitFnImpl(Impl&& impl)
                : impl_(std::move(impl))
            {
            }
            void operator()(DoneCB& doneCB) override { impl_(doneCB); }

        private:
            [[no_unique_address]] Impl impl_;
        };

        struct InitFn {
            std::unique_ptr<InitFnBase> impl_;
            template <class Impl>
            explicit InitFn(Impl&& impl)
                : impl_(std::make_unique<InitFnImpl<Impl>>(
                      std::forward<Impl>(impl)))
            {
            }
            void operator()(DoneCB& doneCB) { (*impl_)(doneCB); }
        };

    public:
        template <class Init, class Args>
        explicit(false) TypeErasedAsioAwaitableFactory(
            AsioAwaitableFactory<ThrowOnError, Init, Args, Ret...>&& rhs)
            : initFn_(std::move(rhs.initFn_))
        {
        }

        DirectAwaitable auto operator co_await() &&
        {
            return AsioAwaitable<InitFn, ThrowOnError, Ret...>(std::move(initFn_));
        }

    private:
        InitFn initFn_;
    };

} // namespace detail

template <>
struct EventLoopTraits<asio::io_context> {
    static EventLoopID EventLoopId(asio::io_context& io)
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

} // namespace corral

namespace asio {

template <class Executor, bool ThrowOnError, class X, class... Ret>
class async_result<::oxygen::co::detail::asio_awaitable_t<Executor, ThrowOnError>,
    X(asio::error_code, Ret...)> {
public:
    using return_type = ::oxygen::co::detail::TypeErasedAsioAwaitableFactory<ThrowOnError,
        Ret...>;

    /// Use `AsioAwaitableFactory` here, so asio::async_*() functions which
    /// don't use `return_type` and instead have `auto` for their return types
    /// will do without type erase.
    template <class Init, class... Args>
    static auto initiate(
        Init&& init,
        ::oxygen::co::detail::asio_awaitable_t<Executor, ThrowOnError>,
        Args... args)
    {
        return ::oxygen::co::detail::AsioAwaitableFactory<
            ThrowOnError, Init, std::tuple<Args...>, Ret...>(
            std::forward<Init>(init), std::move(args)...);
    }
};

} // namespace boost::asio

namespace oxygen::co {
namespace detail {
    /// A helper class for `corral::sleepFor()`.
    class Timer {
    public:
        template <class R, class P>
        Timer(asio::io_context& io, std::chrono::duration<R, P> delay)
            : timer_(io)
        {
            timer_.expires_from_now(delay);
        }

        auto operator co_await() -> DirectAwaitable auto
        {
            return GetAwaitable(timer_.async_wait(asio_awaitable));
        }

    private:
        asio::high_resolution_timer timer_;
    };
} // namespace detail

/// A utility function, returning an awaitable suspending the caller
/// for specified duration. Suitable for use with anyOf() etc.
template <class R, class P>
auto SleepFor(asio::io_context& io, std::chrono::duration<R, P> delay)
{
    return detail::Timer(io, delay);
}

} // namespace oxygen::co
