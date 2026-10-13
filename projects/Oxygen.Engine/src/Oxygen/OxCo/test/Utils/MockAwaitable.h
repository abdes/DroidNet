//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <gmock/gmock.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Coroutine.h>

using oxygen::co::detail::Handle;

namespace oxygen::co::testing {

// Interface for Awaitable
class IAwaitable {
public:
  virtual ~IAwaitable() = default;
  IAwaitable() = default;
  OXYGEN_DEFAULT_COPYABLE(IAwaitable)
  OXYGEN_DEFAULT_MOVABLE(IAwaitable)

  [[nodiscard]] virtual auto await_ready() const noexcept -> bool = 0;
  virtual void await_suspend(Handle h) = 0;
  virtual auto await_resume() -> int = 0;
  virtual auto await_early_cancel() noexcept -> bool = 0;
  virtual auto await_cancel(Handle) noexcept -> bool = 0;
  [[nodiscard]] virtual auto await_must_resume() const noexcept -> bool = 0;
};

class MockAwaitableImpl final : public IAwaitable {
public:
  MOCK_METHOD(bool, await_ready, (),
    (const, noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
  MOCK_METHOD(void, await_suspend, (Handle), (override));
  MOCK_METHOD(int, await_resume, (),
    (override)); // NOLINT(modernize-use-trailing-return-type)
  MOCK_METHOD(bool, await_early_cancel, (),
    (noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
  MOCK_METHOD(bool, await_cancel, (Handle),
    (noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
  MOCK_METHOD(bool, await_must_resume, (),
    (const, noexcept, override)); // NOLINT(modernize-use-trailing-return-type)
};

class MockAwaitable final : public IAwaitable {
public:
  MockAwaitable() noexcept
    : mock_(std::make_shared<MockAwaitableImpl>())
  {
  }
  ~MockAwaitable() override = default;

  // Move constructor just copies the shared_ptr
  MockAwaitable(MockAwaitable&& other) noexcept
    : mock_(other.mock_) // NOLINT(*-move-constructor-init) on purpose
  {
  }
  auto operator=(MockAwaitable&& rhs) noexcept -> MockAwaitable&
  {
    if (this != &rhs) {
      mock_ = rhs.mock_;
    }
    return *this;
  }

  OXYGEN_MAKE_NON_COPYABLE(MockAwaitable)

  // Delegate all methods to mock implementation
  [[nodiscard]] auto await_ready() const noexcept -> bool override
  {
    const auto ret = mock_->await_ready();
    DLOG_F(1, "Aw await_ready() -> {}", ret);
    return ret;
  }
  void await_suspend(const std::coroutine_handle<> h) override
  {
    mock_->await_suspend(h);
    DLOG_F(1, "Aw await_suspend()");
  }
  [[nodiscard]] auto await_resume() -> int override
  {
    const auto ret = mock_->await_resume();
    DLOG_F(1, "Aw await_resume() -> {}", ret);
    return ret;
  }
  [[nodiscard]] auto await_early_cancel() noexcept -> bool override
  {
    const auto ret = mock_->await_early_cancel();
    DLOG_F(1, "Aw await_early_cancel() -> {}", ret);
    return ret;
  }

  [[nodiscard]] auto await_cancel(const std::coroutine_handle<> h) noexcept
    -> bool override
  {
    const auto ret = mock_->await_cancel(h);
    DLOG_F(1, "Aw await_cancel(h) -> {}", ret);
    return ret;
  }
  [[nodiscard]] auto await_must_resume() const noexcept -> bool override
  {
    const auto ret = mock_->await_must_resume();
    DLOG_F(1, "Aw await_must_resume() -> {}", ret);
    return ret;
  }

  // Access to mock for setting expectations
  [[nodiscard]] auto Mock() const -> MockAwaitableImpl* { return mock_.get(); }

private:
  std::shared_ptr<MockAwaitableImpl> mock_;
};

static_assert(Awaitable<MockAwaitable>);

} // namespace oxygen::co::testing
