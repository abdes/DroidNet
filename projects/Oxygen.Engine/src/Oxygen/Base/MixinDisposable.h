//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>

namespace oxygen {

// ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
template <typename Base>
class MixinDisposable : public Base {
public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit MixinDisposable(Args&&... args)
        : Base(std::forward<Args>(args)...)
    {
    }
    virtual ~MixinDisposable()
    {
        if (should_release_) {
            LOG_F(ERROR, "You should call Release() before the Disposable object is destroyed!");
            const auto stack_trace = loguru::stacktrace();
            if (!stack_trace.empty()) {
                DRAW_LOG_F(ERROR, "{}", stack_trace.c_str());
            }
            ABORT_F("Cannot continue!");
        }
    }

    OXYGEN_DEFAULT_COPYABLE(MixinDisposable);
    OXYGEN_DEFAULT_MOVABLE(MixinDisposable);

    void Release() noexcept
    {
        if (!should_release_)
            return;
        this->self().OnRelease();
        should_release_ = false;
        DLOG_F(3, "{} released", this->self().ObjectName());
    }

    [[nodiscard]] auto ShouldRelease() const -> bool { return should_release_; }
    void ShouldRelease(const bool value) { should_release_ = value; }

private:
    bool should_release_ { false };
};

} // namespace oxygen::graphics
