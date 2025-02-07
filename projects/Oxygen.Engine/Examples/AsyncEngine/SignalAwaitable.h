
#pragma once

#include <type_traits>

#include <Oxygen/Base/Detail/signal.hpp>
#include <Oxygen/OxCo/Coroutine.h>

template <typename Callable, typename... Args>
    requires(std::is_invocable_v<Callable, Args...>)
class SignalAwaitable {
    sigslot::signal<Args...>& signal_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    sigslot::connection conn_;
    Callable callable_;

public:
    SignalAwaitable(sigslot::signal<Args...>& signal, Callable callable)
        : signal_(signal)
        , callable_(std::move(callable))
    {
    }

    // ReSharper disable CppMemberFunctionMayBeStatic
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(Handle h)
    {
        conn_ = signal_.connect(
            [this, h](Args&&... args) {
                // Invoke the callable method on obj_ with the arguments
                // received from the signal.
                std::invoke(callable_, std::forward<Args>(args)...);
                // Disconnect the signal after the first emission.
                conn_.disconnect();
                h.resume();
            });
    }
    void await_resume() { /* no return value*/ }
    auto await_cancel(std::coroutine_handle<> /*h*/)
    {
        conn_.disconnect();
        return std::true_type {};
    }
    // ReSharper restore CppMemberFunctionMayBeStatic
};
