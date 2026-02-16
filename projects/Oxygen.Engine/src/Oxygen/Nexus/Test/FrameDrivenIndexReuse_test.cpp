//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <variant>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Nexus/FrameDrivenIndexReuse.h>

using oxygen::graphics::detail::DeferredReclaimer;
using oxygen::nexus::FrameDrivenIndexReuse;
using oxygen::nexus::VersionedIndex;
namespace b = oxygen::bindless;

namespace {

constexpr oxygen::frame::Slot kFrameSlot0 { 0U };
constexpr uint32_t kExpectedRecycleCount1 = 1U;

class GenericFrameDrivenIndexReuseTest : public ::testing::Test { };

NOLINT_TEST_F(
  GenericFrameDrivenIndexReuseTest, ActivateSlotReturnsNewGeneration)
{
  DeferredReclaimer reclaimer;
  std::vector<b::HeapIndex> recycled_indices;
  FrameDrivenIndexReuse<b::HeapIndex> strategy(reclaimer,
    [&](b::HeapIndex idx, std::monostate) { recycled_indices.push_back(idx); });

  constexpr b::HeapIndex kIndex { 10U };
  auto h1 = strategy.ActivateSlot(kIndex);
  EXPECT_EQ(h1.index.get(), kIndex.get());
  EXPECT_GT(h1.generation.get(), 0U);
  EXPECT_TRUE(strategy.IsHandleCurrent(h1));

  reclaimer.OnRendererShutdown();
}

NOLINT_TEST_F(
  GenericFrameDrivenIndexReuseTest, ReleaseInvalidatesHandleImmediately)
{
  DeferredReclaimer reclaimer;
  std::vector<b::HeapIndex> recycled_indices;
  FrameDrivenIndexReuse<b::HeapIndex> strategy(reclaimer,
    [&](b::HeapIndex idx, std::monostate) { recycled_indices.push_back(idx); });

  constexpr b::HeapIndex kIndex { 5U };
  auto h1 = strategy.ActivateSlot(kIndex);

  strategy.Release(h1);

  EXPECT_FALSE(strategy.IsHandleCurrent(h1));

  reclaimer.OnRendererShutdown();
}

NOLINT_TEST_F(
  GenericFrameDrivenIndexReuseTest, ReleaseSchedulesRecycleOnFrameCycle)
{
  DeferredReclaimer reclaimer;
  std::vector<b::HeapIndex> recycled_indices;
  FrameDrivenIndexReuse<b::HeapIndex> strategy(reclaimer,
    [&](b::HeapIndex idx, std::monostate) { recycled_indices.push_back(idx); });

  constexpr b::HeapIndex kIndex { 42U };
  auto h1 = strategy.ActivateSlot(kIndex);

  strategy.Release(h1);

  // Verify not recycled immediately
  EXPECT_TRUE(recycled_indices.empty());
  EXPECT_FALSE(strategy.IsHandleCurrent(h1));

  // Simulate frame cycle to trigger recycle
  reclaimer.OnBeginFrame(kFrameSlot0);

  EXPECT_FALSE(recycled_indices.empty());
  EXPECT_EQ(recycled_indices.back().get(), kIndex.get());

  reclaimer.OnRendererShutdown();
}

NOLINT_TEST_F(GenericFrameDrivenIndexReuseTest, DoubleReleaseIsSafe)
{
  DeferredReclaimer reclaimer;
  std::vector<b::HeapIndex> recycled_indices;
  FrameDrivenIndexReuse<b::HeapIndex> strategy(reclaimer,
    [&](b::HeapIndex idx, std::monostate) { recycled_indices.push_back(idx); });

  constexpr b::HeapIndex kIndex { 99U };
  auto h1 = strategy.ActivateSlot(kIndex);

  strategy.Release(h1);
  strategy.Release(h1); // Should be ignored

  EXPECT_FALSE(strategy.IsHandleCurrent(h1));

  // Verify only one recycle (requires frame cycle)
  reclaimer.OnBeginFrame(kFrameSlot0);
  EXPECT_EQ(recycled_indices.size(), kExpectedRecycleCount1);

  reclaimer.OnRendererShutdown();
}

NOLINT_TEST_F(GenericFrameDrivenIndexReuseTest, ReuseIncrementsGeneration)
{
  DeferredReclaimer reclaimer;
  std::vector<b::HeapIndex> recycled_indices;
  FrameDrivenIndexReuse<b::HeapIndex> strategy(reclaimer,
    [&](b::HeapIndex idx, std::monostate) { recycled_indices.push_back(idx); });

  constexpr b::HeapIndex kIndex { 1U };

  // GEN 1
  auto h1 = strategy.ActivateSlot(kIndex);
  const auto gen1 = h1.generation;
  strategy.Release(h1);

  // GEN 2 (Simulate recycle finished, allocate again)
  reclaimer.OnBeginFrame(kFrameSlot0); // Recycle happens here

  auto h2 = strategy.ActivateSlot(kIndex);
  const auto gen2 = h2.generation;

  EXPECT_GT(gen2.get(), gen1.get());
  EXPECT_TRUE(strategy.IsHandleCurrent(h2));
  EXPECT_FALSE(strategy.IsHandleCurrent(h1));

  reclaimer.OnRendererShutdown();
}

} // namespace
