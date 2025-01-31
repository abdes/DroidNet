//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <memory>

#include <gmock/gmock.h>

#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Detail/MuxBase.h"

namespace oxygen::co::testing {

// Interface for Mux
class IMux {
public:
    IMux() = default;
    virtual ~IMux() = default;
    OXYGEN_DEFAULT_COPYABLE(IMux)
    OXYGEN_DEFAULT_MOVABLE(IMux)

    [[nodiscard]] virtual auto Size() const noexcept -> size_t = 0;
    [[nodiscard]] virtual auto MinReady() const noexcept -> size_t = 0;

    virtual auto InternalCancel() -> bool = 0;
    virtual void Invoke(const std::exception_ptr& ex) = 0;

    [[nodiscard]] virtual auto Parent() const noexcept -> const detail::Handle& = 0;
};

class MockMuxImpl final : public IMux {
public:
    MOCK_METHOD(size_t, Size, (), (const, noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
    MOCK_METHOD(size_t, MinReady, (), (const, noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
    MOCK_METHOD(void, Invoke, (const std::exception_ptr&), (override));
    MOCK_METHOD(bool, InternalCancel, (), (override)); // NOLINT(modernize-use-trailing-return-type)
    MOCK_METHOD(const Handle&, Parent, (), (const, noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
};

class MockMux final : public IMux {
public:
    MockMux() noexcept
        : mock_(std::make_shared<MockMuxImpl>())
    {
    }
    ~MockMux() override = default;

    // Move constructor just copies the shared_ptr

    // Move constructor just copies the shared_ptr
    MockMux(MockMux&& other) noexcept
        : mock_(other.mock_) // NOLINT(performance-move-constructor-init)
        , parent_(other.parent_)
    {
    }
    auto operator=(MockMux&& rhs) noexcept -> MockMux&
    {
        if (this != &rhs) {
            mock_ = rhs.mock_;
            parent_ = rhs.parent_;
        }
        return *this;
    }

    OXYGEN_MAKE_NON_COPYABLE(MockMux)

    // Delegate all methods to mock implementation
    [[nodiscard]] auto Size() const noexcept -> size_t override
    {
        return mock_->Size();
    }
    [[nodiscard]] auto MinReady() const noexcept -> size_t override
    {
        return mock_->MinReady();
    }
    void Invoke(const std::exception_ptr& ex) override
    {
        mock_->Invoke(ex);
        DLOG_F(1, "Mux Invoke({})", ex == nullptr ? "ex" : "nullptr");
    }
    [[nodiscard]] auto InternalCancel() -> bool override
    {
        const auto ret = mock_->InternalCancel();
        DLOG_F(1, "Mux InternalCancel() -> {}", ret);
        return ret;
    }

    // Access to mock for setting expectations
    [[nodiscard]] auto Mock() const -> MockMuxImpl* { return mock_.get(); }

private:
    std::shared_ptr<MockMuxImpl> mock_;
    Handle parent_; // Required for Linkable concept

    [[nodiscard]] auto Parent() const noexcept -> const Handle& override
    {
        return parent_;
    }

    template <class, class>
    friend class oxygen::co::detail::MuxHelper;
};

} // namespace oxygen::co::testing
