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

#include "Oxygen/Base/Macros.h"
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
    // NOLINTBEGIN(*-non-private-member-variables-in-classes)

    template <bool ThrowOnError, class Init, class Args, class... Ret>
    class AsioAwaitable;
    template <bool ThrowOnError, class... Ret>
    class TypeErasedAsioAwaitable;

    template <class... Ret>
    class AsioAwaiterBase {
    protected:
        using Err = asio::error_code;
        struct DoneCB {
            AsioAwaiterBase* aw_;

            void operator()(Err err, Ret... ret) const
            {
                aw_->done(err, std::forward<Ret>(ret)...);
            }

            using cancellation_slot_type = asio::cancellation_slot;
            [[nodiscard]] auto get_cancellation_slot() const -> cancellation_slot_type
            {
                return aw_->cancelSig_.slot();
            }
        };

        void done(const Err ec, Ret... ret)
        {
            ec_ = ec;
            ret_.emplace(std::forward<Ret>(ret)...);
            std::exchange(parent_, std::noop_coroutine())();
        }

        Err ec_;
        std::optional<std::tuple<Ret...>> ret_;
        Handle parent_;
        DoneCB doneCB_ {};
        asio::cancellation_signal cancelSig_;

        template <bool, class, class, class...>
        friend class AsioAwaitable;
        template <bool, class...>
        friend class TypeErasedAsioAwaitable;
    };

    template <class InitFn, bool ThrowOnError, class... Ret>
    class AsioAwaiter : /*private*/ AsioAwaiterBase<Ret...> {
        using Err = asio::error_code;

    public:
        // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved) - perfect forwarding
        explicit AsioAwaiter(InitFn&& initFn)
            : initFn_(std::forward<InitFn>(initFn))
        {
        }

        ~AsioAwaiter() = default;

        OXYGEN_MAKE_NON_MOVABLE(AsioAwaiter)
        OXYGEN_MAKE_NON_COPYABLE(AsioAwaiter)

        //! @{
        //! Implementation of the awaiter interface.
        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTBEGIN(*-convert-member-functions-to-static, *-use-nodiscard)

        auto await_ready() const noexcept { return false; }

        void await_suspend(Handle h)
        {
            this->parent_ = h;
            this->doneCB_ = typename AsioAwaiterBase<Ret...>::DoneCB { this };
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

        auto await_cancel(Handle /*h*/) noexcept -> bool
        {
            this->cancelSig_.emit(asio::cancellation_type::all);
            return false;
        }

        auto await_must_resume() const noexcept -> bool
        {
            return this->ec_ != asio::error::operation_aborted;
        }

        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTEND(*-convert-member-functions-to-static, *-use-nodiscard)
        //! @}

    private:
        [[no_unique_address]] InitFn initFn_;
    };

    template <bool ThrowOnError, class Init, class Args, class... Ret>
    class AsioAwaitable {
        using DoneCB = typename AsioAwaiterBase<Ret...>::DoneCB;

        struct InitFn {
            Init init_;
            [[no_unique_address]] Args args_;

            template <class... Ts>
            // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved) - perfect forwarding
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
        // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved) - perfect forwarding
        explicit AsioAwaitable(Init&& init, Ts&&... args)
            : initFn_(std::forward<Init>(init), std::forward<Ts>(args)...)
        {
        }

        auto operator co_await() && -> Awaiter auto
        {
            return AsioAwaiter<InitFn, ThrowOnError, Ret...>(
                std::forward<InitFn>(initFn_));
        }

    private:
        InitFn initFn_;

        friend TypeErasedAsioAwaitable<ThrowOnError, Ret...>;
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
    class TypeErasedAsioAwaitable {
        using DoneCB = typename AsioAwaiterBase<Ret...>::DoneCB;

        struct InitFnBase {
            InitFnBase() = default;
            virtual ~InitFnBase() = default;
            OXYGEN_DEFAULT_COPYABLE(InitFnBase)
            OXYGEN_DEFAULT_MOVABLE(InitFnBase)
            virtual void operator()(DoneCB&) = 0;
        };

        template <class Impl>
        struct InitFnImpl final : InitFnBase {
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
            explicit InitFn(Impl&& impl) // NOLINT(*-forwarding-reference-overload)
                : impl_(std::make_unique<InitFnImpl<Impl>>(
                      std::forward<Impl>(impl)))
            {
            }
            void operator()(DoneCB& doneCB) { (*impl_)(doneCB); }
        };

    public:
        template <class Init, class Args>
        explicit(false) TypeErasedAsioAwaitable(
            // NOLINTNEXTLINE(*-rvalue-reference-param-not-moved) - moving content
            AsioAwaitable<ThrowOnError, Init, Args, Ret...>&& rhs)
            : initFn_(std::move(rhs.initFn_))
        {
        }

        auto operator co_await() && -> Awaiter auto
        {
            return AsioAwaiter<InitFn, ThrowOnError, Ret...>(std::move(initFn_));
        }

    private:
        InitFn initFn_;
    };

    // NOLINTEND(*-non-private-member-variables-in-classes)
} // namespace detail

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
class asio::async_result<::oxygen::co::detail::asio_awaitable_t<Executor, ThrowOnError>,
    X(asio::error_code, Ret...)> {
public:
    using return_type = ::oxygen::co::detail::TypeErasedAsioAwaitable<ThrowOnError,
        Ret...>;

    /// Use `AsioAwaitable` here, so asio::async_*() functions which
    /// don't use `return_type` and instead have `auto` for their return types
    /// will do without type erase.
    template <class Init, class... Args>
    static auto initiate(
        Init&& init,
        ::oxygen::co::detail::asio_awaitable_t<Executor, ThrowOnError> /*unused*/,
        Args... args)
    {
        return ::oxygen::co::detail::AsioAwaitable<
            ThrowOnError, Init, std::tuple<Args...>, Ret...>(
            std::forward<Init>(init), std::move(args)...);
    }
}; // namespace asio

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

        auto operator co_await() -> Awaiter auto
        {
            return GetAwaiter(timer_.async_wait(asio_awaitable));
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
