//===----------------------------------------------------------------------===//
// Focused tests for FrameDrivenSlotReuse under DomainToken semantics
//===----------------------------------------------------------------------===//

#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Nexus/FrameDrivenSlotReuse.h>
#include <Oxygen/Nexus/Test/NexusMocks.h>

using oxygen::VersionedBindlessHandle;
using oxygen::nexus::DomainKey;
using oxygen::nexus::testing::AllocateBackend;
using oxygen::nexus::testing::FreeBackend;

namespace b = oxygen::bindless;
namespace g = oxygen::bindless::generated;

namespace {

constexpr oxygen::frame::Slot kFrameSlot0 { 0U };

NOLINT_TEST(FrameDrivenSlotReuse,
  AllocateReleaseAfterFrameCycle_ReusesSlotWithIncrementedGeneration)
{
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .domain = g::kTexturesDomain };
  const auto first = reuse.Allocate(domain);
  ASSERT_TRUE(first.IsValid());

  reuse.Release(domain, first);
  const auto before_reclaim = reuse.Allocate(domain);
  EXPECT_NE(before_reclaim.ToBindlessHandle(), first.ToBindlessHandle());

  per_frame.OnBeginFrame(kFrameSlot0);
  const auto second = reuse.Allocate(domain);
  EXPECT_EQ(second.ToBindlessHandle(), first.ToBindlessHandle());
  EXPECT_EQ(second.GenerationValue().get(), first.GenerationValue().get() + 1U);
}

NOLINT_TEST(FrameDrivenSlotReuse, DoubleRelease_IsIgnoredViaGenerationCheck)
{
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .domain = g::kGlobalSrvDomain };
  const auto handle = reuse.Allocate(domain);
  reuse.Release(domain, handle);
  reuse.Release(domain, handle);
  per_frame.OnBeginFrame(kFrameSlot0);

  const auto next = reuse.Allocate(domain);
  EXPECT_EQ(next.ToBindlessHandle(), handle.ToBindlessHandle());
  EXPECT_EQ(next.GenerationValue().get(), handle.GenerationValue().get() + 1U);
}

NOLINT_TEST(FrameDrivenSlotReuse, ConcurrentReleases_ReclaimAllHandles)
{
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .domain = g::kMaterialsDomain };
  std::vector<VersionedBindlessHandle> handles;
  for (int i = 0; i < 128; ++i) {
    handles.push_back(reuse.Allocate(domain));
  }

  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([&, t]() {
      for (size_t i = t; i < handles.size(); i += 4) {
        reuse.Release(domain, handles[i]);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  per_frame.OnBeginFrame(kFrameSlot0);
  EXPECT_EQ(do_free.freed.size(), handles.size());
}

} // namespace
