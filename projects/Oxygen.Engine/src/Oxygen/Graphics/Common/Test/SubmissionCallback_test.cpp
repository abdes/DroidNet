//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::graphics::SubmissionCallback;
using oxygen::graphics::SubmissionOutcome;

static_assert(!std::is_copy_constructible_v<SubmissionCallback>);
static_assert(!std::is_copy_assignable_v<SubmissionCallback>);
static_assert(std::is_nothrow_move_constructible_v<SubmissionCallback>);
static_assert(std::is_nothrow_move_assignable_v<SubmissionCallback>);

struct Lifetime {
  explicit Lifetime(int& count)
    : destroyed(&count)
  {
  }
  ~Lifetime() { ++*destroyed; }
  Lifetime(const Lifetime&) = delete;
  auto operator=(const Lifetime&) -> Lifetime& = delete;
  Lifetime(Lifetime&&) = delete;
  auto operator=(Lifetime&&) -> Lifetime& = delete;
  int* destroyed;
};

NOLINT_TEST(SubmissionCallbackTest, EmptyCallbackRejectsInvocation)
{
  SubmissionCallback callback;
  EXPECT_FALSE(callback);
  EXPECT_THROW(callback(SubmissionOutcome::kDiscarded), std::bad_function_call);
}

NOLINT_TEST(SubmissionCallbackTest, MovingSmallCaptureTransfersSoleOwnership)
{
  int destroyed = 0;
  int calls = 0;
  {
    SubmissionCallback callback(
      [owner = std::make_unique<Lifetime>(destroyed), &calls](
        const SubmissionOutcome outcome) -> void {
        EXPECT_NE(owner, nullptr);
        EXPECT_EQ(outcome, SubmissionOutcome::kSubmitted);
        ++calls;
      });
    auto moved = SubmissionCallback(std::move(callback));
    EXPECT_TRUE(moved);
    EXPECT_EQ(destroyed, 0);
    moved(SubmissionOutcome::kSubmitted);
    EXPECT_EQ(calls, 1);
  }
  EXPECT_EQ(destroyed, 1);
}

NOLINT_TEST(SubmissionCallbackTest, MoveAssignmentReleasesPreviousCaptureOnce)
{
  int destroyed = 0;
  {
    SubmissionCallback first(
      [owner = std::make_unique<Lifetime>(destroyed)](
        SubmissionOutcome) -> void { EXPECT_NE(owner, nullptr); });
    SubmissionCallback second(
      [owner = std::make_unique<Lifetime>(destroyed)](
        SubmissionOutcome) -> void { EXPECT_NE(owner, nullptr); });
    second = std::move(first);
    EXPECT_TRUE(second);
    EXPECT_EQ(destroyed, 1);
    auto& alias = second;
    second = std::move(alias);
    EXPECT_TRUE(second);
    EXPECT_EQ(destroyed, 1);
  }
  EXPECT_EQ(destroyed, 2);
}

NOLINT_TEST(
  SubmissionCallbackTest, LargeOverAlignedCaptureKeepsOwnershipAndAlignment)
{
  constexpr size_t kCaptureAlignment = 128U;
  struct alignas(kCaptureAlignment) Capture {
    std::unique_ptr<Lifetime> owner;
    int* calls;
    void operator()([[maybe_unused]] SubmissionOutcome outcome)
    {
      EXPECT_NE(owner, nullptr);
      ++*calls;
    }
  };
  int destroyed = 0;
  int calls = 0;
  {
    SubmissionCallback callback(Capture {
      .owner = std::make_unique<Lifetime>(destroyed), .calls = &calls });
    auto moved = SubmissionCallback(std::move(callback));
    EXPECT_EQ(destroyed, 0);
    moved(SubmissionOutcome::kDiscarded);
    EXPECT_EQ(calls, 1);
  }
  EXPECT_EQ(destroyed, 1);
}

NOLINT_TEST(
  SubmissionCallbackTest, CallbackMoveDoesNotCallPotentiallyThrowingTargetMove)
{
  struct Capture {
    int* moves;
    explicit Capture(int& count)
      : moves(&count)
    {
    }
    Capture(Capture&& other) noexcept(false)
      : moves(other.moves)
    {
      if (++*moves > 1) {
        throw std::runtime_error(
          "Callback movement must not move a throwing target");
      }
    }
    ~Capture() = default;
    Capture(const Capture&) = delete;
    auto operator=(const Capture&) -> Capture& = delete;
    auto operator=(Capture&&) -> Capture& = delete;
    void operator()([[maybe_unused]] SubmissionOutcome outcome) { }
  };
  int moves = 0;
  SubmissionCallback callback(Capture { moves });
  EXPECT_EQ(moves, 1);
  auto moved = SubmissionCallback(std::move(callback));
  EXPECT_TRUE(moved);
  EXPECT_EQ(moves, 1);
}

} // namespace
