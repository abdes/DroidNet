//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::Format;
using oxygen::ViewId;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::internal::RetainedTexturePool;
using oxygen::vortex::testing::FakeGraphics;
namespace frame = oxygen::frame;

constexpr ViewId kView {
  51U,
};

auto MakeDesc() -> TextureDesc
{
  return {
    .width = 16U,
    .height = 8U,
    .format = Format::kRGBA16Float,
    .debug_name = "Retained output",
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  };
}

auto SetState(CommandQueue& queue, const Texture& texture,
  const ResourceStates state) -> void
{
  const std::array states {
    CommandQueue::KnownResourceState {
      .resource = texture.GetNativeResource(),
      .state = state,
    },
  };
  queue.AdoptKnownResourceStates(states);
}

class NamedQueueStrategy final : public oxygen::graphics::QueuesStrategy {
public:
  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<NamedQueueStrategy>(*this);
  }
  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    using oxygen::graphics::QueueAllocationPreference;
    using oxygen::graphics::QueueKey;
    using oxygen::graphics::QueueSharingPreference;
    return {
      { .key = QueueKey { "graphics", },
        .role = QueueRole::kGraphics,
        .allocation_preference = QueueAllocationPreference::kDedicated,
        .sharing_preference = QueueSharingPreference::kShared, },
      { .key = QueueKey { "named-secondary", },
        .role = QueueRole::kGraphics,
        .allocation_preference = QueueAllocationPreference::kDedicated,
        .sharing_preference = QueueSharingPreference::kNamed, },
    };
  }
  [[nodiscard]] auto KeyFor(QueueRole /*role*/) const
    -> oxygen::graphics::QueueKey override
  {
    return oxygen::graphics::QueueKey {
      "graphics",
    };
  }
};

class RetainedTexturePoolTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy {});
    pool_ = std::make_unique<RetainedTexturePool>(graphics_);
    pool_->OnFrameStart(frame::SequenceNumber {
      1U,
    });
    RetireSlot(0U);
  }
  auto TearDown() -> void override
  {
    pool_.reset();
    if (graphics_) {
      graphics_->GetDeferredReclaimer().ProcessAllDeferredReleases();
    }
  }
  auto RetireSlot(const unsigned slot) -> void
  {
    graphics_->GetDeferredReclaimer().OnBeginFrame(frame::Slot {
      slot,
    });
  }
  auto Acquire(const ViewId view = kView, const bool recyclable = true)
    -> std::shared_ptr<Texture>
  {
    auto texture = pool_->Acquire(view, MakeDesc(), recyclable);
    if (texture) {
      SetState(*graphics_->GetCommandQueue(QueueRole::kGraphics), *texture,
        ResourceStates::kShaderResource);
    }
    return texture;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::unique_ptr<RetainedTexturePool> pool_;
};

TEST_F(
  RetainedTexturePoolTest, LastReaderAndItsFenceProtectTextureAndDescriptors)
{
  auto first = Acquire();
  ASSERT_NE(first, nullptr);
  auto reader = first;
  const auto* identity = first.get();
  const std::weak_ptr<Texture> physical = first->shared_from_this();
  auto& registry = graphics_->GetResourceRegistry();
  const auto view_desc = oxygen::graphics::TextureViewDescription {
    .view_type = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
    .format = first->GetDescriptor().format,
    .dimension = first->GetDescriptor().texture_type,
  };
  auto& allocator = graphics_->GetDescriptorAllocator();
  auto allocation
    = allocator.AllocateRaw(view_desc.view_type, view_desc.visibility);
  ASSERT_TRUE(allocation.IsValid());
  const auto descriptor = allocator.GetShaderVisibleIndex(allocation);
  ASSERT_TRUE(
    registry.RegisterView(*first, std::move(allocation), view_desc)->IsValid());

  first.reset();
  RetireSlot(0U);
  EXPECT_EQ(registry.FindShaderVisibleIndex(*reader, view_desc), descriptor);
  auto concurrent = Acquire();
  EXPECT_NE(concurrent.get(), reader.get());
  RetireSlot(1U);
  reader.reset();
  RetireSlot(0U);
  ASSERT_FALSE(physical.expired());
  EXPECT_TRUE(registry.Contains(*physical.lock()));
  RetireSlot(1U);
  ASSERT_FALSE(physical.expired());
  EXPECT_FALSE(registry.Contains(*physical.lock()));

  auto reused = Acquire();
  EXPECT_EQ(reused.get(), identity);
  EXPECT_TRUE(registry.Contains(*reused));
  EXPECT_FALSE(registry.FindShaderVisibleIndex(*reused, view_desc).has_value());
}

TEST_F(RetainedTexturePoolTest, UnderlyingExternalOwnerDisqualifiesRecycling)
{
  auto texture = Acquire();
  auto underlying = texture->shared_from_this();
  const std::weak_ptr<Texture> physical = underlying;
  texture.reset();
  RetireSlot(0U);
  EXPECT_FALSE(graphics_->GetResourceRegistry().Contains(*underlying));
  auto next = Acquire();
  EXPECT_NE(next.get(), underlying.get());
  underlying.reset();
  EXPECT_TRUE(physical.expired());
}

TEST_F(RetainedTexturePoolTest, KeepsAtMostOneIdleTexturePerView)
{
  auto first = Acquire();
  auto second = Acquire();
  const auto* identity = first.get();
  const std::weak_ptr<Texture> first_physical = first->shared_from_this();
  const std::weak_ptr<Texture> second_physical = second->shared_from_this();
  first.reset();
  second.reset();
  RetireSlot(0U);
  EXPECT_FALSE(first_physical.expired());
  EXPECT_TRUE(second_physical.expired());
  EXPECT_EQ(Acquire().get(), identity);
}

TEST_F(RetainedTexturePoolTest, UnderlyingOwnerAcquiredWhileIdlePreventsReuse)
{
  auto texture = Acquire();
  const std::weak_ptr<Texture> physical = texture->shared_from_this();
  texture.reset();
  RetireSlot(0U);
  auto observer = physical.lock();
  ASSERT_NE(observer, nullptr);
  auto next = Acquire();
  EXPECT_NE(next.get(), observer.get());
  EXPECT_FALSE(graphics_->GetResourceRegistry().Contains(*observer));
  observer.reset();
  EXPECT_TRUE(physical.expired());
}

TEST_F(RetainedTexturePoolTest, DescriptorReplacementInvalidatesLateReturns)
{
  for (unsigned change = 0U; change < 3U; ++change) {
    SCOPED_TRACE(change);
    auto old = Acquire();
    const std::weak_ptr<Texture> old_physical = old->shared_from_this();
    auto desc = MakeDesc();
    if (change == 0U) {
      desc.width *= 2U;
    } else if (change == 1U) {
      desc.format = Format::kRGBA32Float;
    } else {
      desc.is_uav = false;
    }
    auto replacement = pool_->Acquire(kView, desc, true);
    ASSERT_NE(replacement, nullptr);
    SetState(*graphics_->GetCommandQueue(QueueRole::kGraphics), *replacement,
      ResourceStates::kShaderResource);
    const auto* replacement_identity = replacement.get();
    old.reset();
    replacement.reset();
    RetireSlot(0U);
    EXPECT_TRUE(old_physical.expired());
    auto reused = pool_->Acquire(kView, desc, true);
    EXPECT_EQ(reused.get(), replacement_identity);
    pool_->Clear();
    reused.reset();
    RetireSlot(0U);
  }
}

TEST_F(RetainedTexturePoolTest, RemovedViewCannotBeRecreatedByLateReturn)
{
  auto old = Acquire();
  const std::weak_ptr<Texture> old_physical = old->shared_from_this();
  pool_->RemoveView(kView);
  auto replacement = Acquire();
  const auto* replacement_identity = replacement.get();
  old.reset();
  replacement.reset();
  RetireSlot(0U);
  EXPECT_TRUE(old_physical.expired());
  EXPECT_EQ(Acquire().get(), replacement_identity);
}

TEST_F(RetainedTexturePoolTest, ClearDropsIdleAndInvalidatesOutstandingReturns)
{
  auto idle = Acquire();
  const std::weak_ptr<Texture> idle_physical = idle->shared_from_this();
  idle.reset();
  RetireSlot(0U);
  auto outstanding = Acquire(ViewId {
    52U,
  });
  const std::weak_ptr<Texture> outstanding_physical
    = outstanding->shared_from_this();
  pool_->Clear();
  EXPECT_TRUE(idle_physical.expired());
  outstanding.reset();
  RetireSlot(0U);
  EXPECT_TRUE(outstanding_physical.expired());
}

TEST_F(RetainedTexturePoolTest, PrunesOnlyViewsAbsentFromPreviousFrame)
{
  auto active = Acquire();
  auto stale = Acquire(ViewId {
    52U,
  });
  const auto* active_identity = active.get();
  const std::weak_ptr<Texture> stale_physical = stale->shared_from_this();
  active.reset();
  RetireSlot(0U);
  pool_->OnFrameStart(frame::SequenceNumber {
    2U,
  });
  active = Acquire();
  EXPECT_EQ(active.get(), active_identity);
  active.reset();
  RetireSlot(0U);
  pool_->OnFrameStart(frame::SequenceNumber {
    3U,
  });
  stale.reset();
  RetireSlot(0U);
  EXPECT_TRUE(stale_physical.expired());
  EXPECT_EQ(Acquire().get(), active_identity);
}

TEST_F(RetainedTexturePoolTest, StatelessOutputsDoNotPopulateIdleCache)
{
  auto persistent = Acquire();
  const std::weak_ptr<Texture> previous = persistent->shared_from_this();
  persistent.reset();
  RetireSlot(0U);
  for (unsigned index = 0U; index < 8U; ++index) {
    auto stateless = Acquire(
      ViewId {
        kView.get() + index,
      },
      false);
    const std::weak_ptr<Texture> physical = stateless->shared_from_this();
    stateless.reset();
    RetireSlot(0U);
    EXPECT_TRUE(physical.expired());
  }
  EXPECT_TRUE(previous.expired());
}

TEST_F(
  RetainedTexturePoolTest, RetainedReaderOutlivesPoolWithoutKeepingIdleCache)
{
  auto retained = Acquire();
  const std::weak_ptr<Texture> physical = retained->shared_from_this();
  pool_.reset();
  RetireSlot(0U);
  EXPECT_TRUE(graphics_->GetResourceRegistry().Contains(*retained));
  retained.reset();
  EXPECT_FALSE(physical.expired());
  RetireSlot(0U);
  EXPECT_TRUE(physical.expired());
}

TEST_F(RetainedTexturePoolTest, FinalReaderCanReleaseTheLastGraphicsOwner)
{
  auto retained = Acquire();
  std::weak_ptr<FakeGraphics> graphics_lifetime = graphics_;
  const std::weak_ptr<Texture> physical = retained->shared_from_this();
  pool_.reset();
  graphics_.reset();
  EXPECT_FALSE(graphics_lifetime.expired());
  retained.reset();
  EXPECT_TRUE(graphics_lifetime.expired());
  EXPECT_TRUE(physical.expired());
}

TEST_F(RetainedTexturePoolTest, WeakWrapperObserverDoesNotRetainGraphics)
{
  auto retained = Acquire();
  const std::weak_ptr<Texture> wrapper_observer = retained;
  const std::weak_ptr<Texture> physical = retained->shared_from_this();
  const std::weak_ptr<FakeGraphics> graphics_lifetime = graphics_;
  pool_.reset();
  retained.reset();
  EXPECT_TRUE(wrapper_observer.expired());
  EXPECT_FALSE(physical.expired());
  RetireSlot(0U);
  EXPECT_TRUE(physical.expired());
  graphics_.reset();
  EXPECT_TRUE(graphics_lifetime.expired());
}

TEST_F(RetainedTexturePoolTest, RestoresActualNonCommonStateAfterRegistration)
{
  auto texture = Acquire();
  const auto native = texture->GetNativeResource();
  const auto* identity = texture.get();
  auto queue = graphics_->GetCommandQueue(QueueRole::kGraphics);
  SetState(*queue, *texture, ResourceStates::kCopySource);
  texture.reset();
  RetireSlot(0U);
  EXPECT_FALSE(queue->TryGetKnownResourceState(native).has_value());
  auto reused = pool_->Acquire(kView, MakeDesc(), true);
  EXPECT_EQ(reused.get(), identity);
  EXPECT_EQ(
    queue->TryGetKnownResourceState(native), ResourceStates::kCopySource);
}

TEST_F(RetainedTexturePoolTest, UnknownStateDiscardsInsteadOfAssumingCommon)
{
  auto texture = pool_->Acquire(kView, MakeDesc(), true);
  const std::weak_ptr<Texture> physical = texture->shared_from_this();
  EXPECT_FALSE(graphics_->TryGetKnownResourceState(texture->GetNativeResource())
      .has_value());
  texture.reset();
  RetireSlot(0U);
  EXPECT_TRUE(physical.expired());
}

TEST_F(
  RetainedTexturePoolTest, NamedQueueConflictDiscardsInsteadOfUsingRoleState)
{
  graphics_->CreateCommandQueues(NamedQueueStrategy {});
  auto texture = Acquire();
  const auto native = texture->GetNativeResource();
  auto named = graphics_->GetCommandQueue(oxygen::graphics::QueueKey {
    "named-secondary",
  });
  ASSERT_NE(named, nullptr);
  ASSERT_NE(named, graphics_->GetCommandQueue(QueueRole::kGraphics));
  EXPECT_EQ(graphics_->TryGetKnownResourceState(native),
    ResourceStates::kShaderResource);
  SetState(*named, *texture, ResourceStates::kShaderResource);
  EXPECT_EQ(graphics_->TryGetKnownResourceState(native),
    ResourceStates::kShaderResource);
  SetState(*named, *texture, ResourceStates::kCopySource);
  EXPECT_FALSE(graphics_->TryGetKnownResourceState(native).has_value());
  const std::weak_ptr<Texture> physical = texture->shared_from_this();
  texture.reset();
  RetireSlot(0U);
  EXPECT_TRUE(physical.expired());
  EXPECT_FALSE(named->TryGetKnownResourceState(native).has_value());
}

TEST_F(RetainedTexturePoolTest, UndefinedTrackedStateAlsoPreventsReuse)
{
  auto texture = Acquire();
  SetState(*graphics_->GetCommandQueue(QueueRole::kGraphics), *texture,
    ResourceStates::kUndefined);
  const std::weak_ptr<Texture> physical = texture->shared_from_this();
  texture.reset();
  RetireSlot(0U);
  EXPECT_TRUE(physical.expired());
}

} // namespace
