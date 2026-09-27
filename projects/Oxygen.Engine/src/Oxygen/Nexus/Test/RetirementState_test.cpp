//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Nexus/RetirementState.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::nexus::FinalizeDisposition;
using oxygen::nexus::IndexReuse;
using oxygen::nexus::RetirementState;

NOLINT_TEST(RetirementStateTest, InactiveConstructionCannotRetireOrAcquire)
{
  RetirementState<std::uint32_t> state;
  EXPECT_FALSE(state.IsActive());
  EXPECT_FALSE(state.AcquireOwner());
  EXPECT_FALSE(state.AcquireUse());
  EXPECT_FALSE(state.AcquireRetainedPin());
  EXPECT_FALSE(state.BeginRetirement());
  EXPECT_FALSE(state.SetRetirement(std::nullopt));
  EXPECT_FALSE(state.Finalize());
  EXPECT_FALSE(state.TakeOrdinaryDrained());
}

NOLINT_TEST(
  RetirementStateTest, OrdinaryAdmissionDrainsBeforeCapturedGeneration)
{
  IndexReuse<std::uint32_t> reuse;
  const auto handle = reuse.ActivateSlot(0U);
  RetirementState<std::uint32_t> state;
  ASSERT_TRUE(state.Activate(1U));
  ASSERT_TRUE(state.AcquireUse());
  ASSERT_TRUE(state.AcquireRetainedPin());
  ASSERT_TRUE(state.AcquireRetainedPin());
  ASSERT_TRUE(state.ReleaseOwner());
  EXPECT_FALSE(state.AcquireOwner());
  EXPECT_FALSE(state.AcquireUse());
  EXPECT_FALSE(state.AcquireRetainedPin());
  EXPECT_FALSE(state.TakeOrdinaryDrained());
  ASSERT_TRUE(state.BeginRetirement());
  EXPECT_FALSE(state.BeginRetirement());
  auto ticket = reuse.TryRetire(handle);
  ASSERT_TRUE(ticket);
  ASSERT_TRUE(state.SetRetirement(std::move(*ticket)));
  EXPECT_FALSE(reuse.IsHandleCurrent(handle));
  EXPECT_FALSE(state.Finalize());
  ASSERT_TRUE(state.ReleaseUse());
  EXPECT_TRUE(state.TakeOrdinaryDrained());
  EXPECT_FALSE(state.TakeOrdinaryDrained());
  EXPECT_FALSE(state.Finalize());
  ASSERT_TRUE(state.ReleaseRetainedPin());
  EXPECT_FALSE(state.Finalize());
  ASSERT_TRUE(state.ReleaseRetainedPin());
  const auto result = state.Finalize();
  if (!result) {
    FAIL() << "Retirement did not finalize";
    return;
  }
  EXPECT_EQ(result->disposition, FinalizeDisposition::kReusable);
  EXPECT_EQ(result->index, 0U);
  EXPECT_TRUE(state.IsResolved());
  EXPECT_FALSE(state.Finalize());
  EXPECT_FALSE(state.SetRetirement(std::nullopt));
  EXPECT_FALSE(state.Activate());
  EXPECT_NE(reuse.ActivateSlot(0U).generation, handle.generation);
}

NOLINT_TEST(RetirementStateTest, MultipleOwnersSealOnlyOnLastRelease)
{
  RetirementState<std::uint32_t> state;
  ASSERT_TRUE(state.Activate());
  EXPECT_FALSE(state.AcquireUse());
  EXPECT_FALSE(state.AcquireRetainedPin());
  ASSERT_TRUE(state.AcquireOwner());
  ASSERT_TRUE(state.AcquireOwner());
  ASSERT_TRUE(state.ReleaseOwner());
  EXPECT_FALSE(state.BeginRetirement());
  ASSERT_TRUE(state.AcquireUse());
  ASSERT_TRUE(state.ReleaseOwner());
  ASSERT_TRUE(state.BeginRetirement());
  ASSERT_TRUE(state.SetRetirement(std::nullopt));
  EXPECT_FALSE(state.Finalize());
  ASSERT_TRUE(state.ReleaseUse());
  const auto result = state.Finalize();
  if (!result) {
    FAIL() << "Retirement did not finalize";
    return;
  }
  EXPECT_EQ(result->disposition, FinalizeDisposition::kClosed);
  EXPECT_FALSE(result->index);
}

NOLINT_TEST(RetirementStateTest, ActivatedOwnerlessRollbackReturnsItsSlot)
{
  IndexReuse<std::uint32_t> reuse;
  const auto handle = reuse.ActivateSlot(0U);
  RetirementState<std::uint32_t> state;
  ASSERT_TRUE(state.Activate());
  EXPECT_FALSE(state.TakeOrdinaryDrained());
  ASSERT_TRUE(state.BeginRetirement());
  EXPECT_FALSE(state.AcquireOwner());
  auto ticket = reuse.TryRetire(handle);
  ASSERT_TRUE(ticket);
  ASSERT_TRUE(state.SetRetirement(std::move(*ticket)));
  const auto result = state.Finalize();
  if (!result) {
    FAIL() << "Retirement did not finalize";
    return;
  }
  EXPECT_EQ(result->index, 0U);
  EXPECT_TRUE(state.TakeOrdinaryDrained());
  EXPECT_FALSE(state.TakeOrdinaryDrained());
}

NOLINT_TEST(RetirementStateTest, DuplicateReleaseLeavesOtherCountsIntact)
{
  RetirementState<std::uint32_t> state;
  EXPECT_FALSE(state.ReleaseOwner());
  EXPECT_FALSE(state.ReleaseUse());
  EXPECT_FALSE(state.ReleaseRetainedPin());
  ASSERT_TRUE(state.Activate(1U));
  ASSERT_TRUE(state.AcquireUse());
  ASSERT_TRUE(state.AcquireRetainedPin());
  ASSERT_TRUE(state.ReleaseOwner());
  EXPECT_FALSE(state.ReleaseOwner());
  EXPECT_EQ(state.UseCount(), 1U);
  EXPECT_EQ(state.RetainedPinCount(), 1U);
  ASSERT_TRUE(state.ReleaseUse());
  EXPECT_FALSE(state.ReleaseUse());
  EXPECT_EQ(state.RetainedPinCount(), 1U);
  ASSERT_TRUE(state.ReleaseRetainedPin());
  EXPECT_FALSE(state.ReleaseRetainedPin());
}

NOLINT_TEST(RetirementStateTest, ClosedPoolDrainsWithoutReusingIndex)
{
  IndexReuse<std::uint32_t> reuse;
  const auto handle = reuse.ActivateSlot(0U);
  RetirementState<std::uint32_t> state;
  ASSERT_TRUE(state.Activate(1U));
  ASSERT_TRUE(state.AcquireUse());
  ASSERT_TRUE(state.ReleaseOwner());
  ASSERT_TRUE(state.BeginRetirement());
  auto ticket = reuse.TryRetire(handle);
  ASSERT_TRUE(ticket);
  ASSERT_TRUE(state.SetRetirement(std::move(*ticket)));
  reuse.Close();
  ASSERT_TRUE(state.ReleaseUse());
  const auto result = state.Finalize();
  if (!result) {
    FAIL() << "Retirement did not finalize";
    return;
  }
  EXPECT_EQ(result->disposition, FinalizeDisposition::kClosed);
  EXPECT_FALSE(result->index);
}

NOLINT_TEST(RetirementStateTest, ExpiredPoolStillRequiresPinDrain)
{
  RetirementState<std::uint32_t> state;
  {
    IndexReuse<std::uint32_t> reuse;
    const auto handle = reuse.ActivateSlot(0U);
    ASSERT_TRUE(state.Activate(1U));
    ASSERT_TRUE(state.AcquireRetainedPin());
    ASSERT_TRUE(state.ReleaseOwner());
    ASSERT_TRUE(state.BeginRetirement());
    auto ticket = reuse.TryRetire(handle);
    ASSERT_TRUE(ticket);
    ASSERT_TRUE(state.SetRetirement(std::move(*ticket)));
  }
  EXPECT_FALSE(state.Finalize());
  ASSERT_TRUE(state.ReleaseRetainedPin());
  const auto result = state.Finalize();
  if (!result) {
    FAIL() << "Retirement did not finalize";
    return;
  }
  EXPECT_EQ(result->disposition, FinalizeDisposition::kClosed);
  EXPECT_FALSE(result->index);
}

NOLINT_TEST(RetirementStateTest, OwnerOverflowDoesNotWrapOrSeal)
{
  RetirementState<std::uint32_t> state;
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  ASSERT_TRUE(state.Activate(maximum));
  EXPECT_FALSE(state.AcquireOwner());
  EXPECT_EQ(state.OwnerCount(), maximum);
  ASSERT_TRUE(state.ReleaseOwner());
  EXPECT_EQ(state.OwnerCount(), maximum - 1U);
  EXPECT_TRUE(state.AcquireOwner());
  EXPECT_FALSE(state.BeginRetirement());
}
} // namespace
