//===----------------------------------------------------------------------===//
// Focused tests for TimelineGatedSlotReuse under DomainToken semantics
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Nexus/Test/NexusMocks.h>
#include <Oxygen/Nexus/TimelineGatedSlotReuse.h>

using oxygen::VersionedBindlessHandle;
using oxygen::nexus::DomainKey;
using oxygen::nexus::testing::AllocateBackend;
using oxygen::nexus::testing::FakeCommandQueue;
using oxygen::nexus::testing::FreeBackend;

namespace b = oxygen::bindless;
namespace g = oxygen::bindless::generated;

namespace {

class TimelineGatedSlotReuseTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    strategy_ = std::make_unique<oxygen::nexus::TimelineGatedSlotReuse>(
      [this](DomainKey d) { return alloc_(d); },
      [this](DomainKey d, b::HeapIndex h) {
        free_(d, h);
        alloc_.free_list.push_back(h.get());
      });
  }

  auto Strategy() -> auto& { return *strategy_; }

  AllocateBackend alloc_;
  FreeBackend free_;
  std::unique_ptr<oxygen::nexus::TimelineGatedSlotReuse> strategy_;
};

NOLINT_TEST_F(TimelineGatedSlotReuseTest,
  ReleaseAfterFenceCompletion_ReclaimsAndBumpsGeneration)
{
  constexpr DomainKey domain { .domain = g::kTexturesDomain };
  auto queue = std::make_shared<FakeCommandQueue>();

  const auto handle = Strategy().Allocate(domain);
  ASSERT_TRUE(handle.IsValid());

  Strategy().Release(
    domain, handle, queue, oxygen::graphics::FenceValue { 1U });
  Strategy().ProcessFor(queue);
  EXPECT_TRUE(Strategy().IsHandleCurrent(handle));

  queue->Signal(1U);
  Strategy().ProcessFor(queue);
  EXPECT_FALSE(Strategy().IsHandleCurrent(handle));
  ASSERT_EQ(free_.freed.size(), 1U);

  const auto next = Strategy().Allocate(domain);
  EXPECT_EQ(next.ToBindlessHandle(), handle.ToBindlessHandle());
  EXPECT_EQ(next.GenerationValue().get(), handle.GenerationValue().get() + 1U);
}

NOLINT_TEST_F(TimelineGatedSlotReuseTest, DuplicateRelease_IsIgnored)
{
  constexpr DomainKey domain { .domain = g::kMaterialsDomain };
  auto queue = std::make_shared<FakeCommandQueue>();

  const auto handle = Strategy().Allocate(domain);
  Strategy().Release(
    domain, handle, queue, oxygen::graphics::FenceValue { 2U });
  Strategy().Release(
    domain, handle, queue, oxygen::graphics::FenceValue { 2U });

  queue->Signal(2U);
  Strategy().ProcessFor(queue);
  EXPECT_EQ(free_.freed.size(), 1U);
}

NOLINT_TEST_F(
  TimelineGatedSlotReuseTest, ReleaseBatch_ReclaimsAllCompletedItems)
{
  constexpr DomainKey domain { .domain = g::kGlobalSrvDomain };
  auto queue = std::make_shared<FakeCommandQueue>();

  const auto a = Strategy().Allocate(domain);
  const auto bhandle = Strategy().Allocate(domain);
  const std::array<std::pair<DomainKey, VersionedBindlessHandle>, 2> batch { {
    { domain, a },
    { domain, bhandle },
  } };

  Strategy().ReleaseBatch(
    queue, oxygen::graphics::FenceValue { 3U }, std::span(batch));
  queue->Signal(3U);
  Strategy().ProcessFor(queue);

  EXPECT_EQ(free_.freed.size(), 2U);
}

} // namespace
