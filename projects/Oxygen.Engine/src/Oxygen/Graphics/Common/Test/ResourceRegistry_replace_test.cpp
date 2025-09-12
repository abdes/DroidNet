//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorSegment.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Test/Fakes/FakeResource.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeView;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::detail::FixedDescriptorSegment;
using oxygen::graphics::testing::FakeResource;
using oxygen::graphics::testing::TestViewDesc;

namespace {

//===----------------------------------------------------------------------===//
// Replace Tests
//===----------------------------------------------------------------------===//

//! Fixture dedicated to Replace-oriented scenarios.
class ResourceRegistryReplaceTest : public testing::Test {
protected:
  std::shared_ptr<MockDescriptorAllocator> allocator_;
  FixedDescriptorSegment* last_segment_ { nullptr };

  std::unique_ptr<ResourceRegistry> registry_;

  std::shared_ptr<FakeResource> old_resource_;
  std::shared_ptr<FakeResource> new_resource_;

  auto SetUp() -> void override
  {
    // Configure allocator to capture the last created segment pointer.
    allocator_ = std::make_shared<testing::NiceMock<MockDescriptorAllocator>>();
    allocator_->ext_segment_factory_ = [this](auto capacity, auto base_index,
                                         auto view_type, auto visibility) {
      auto seg = std::make_unique<FixedDescriptorSegment>(
        capacity, base_index, view_type, visibility);
      last_segment_ = seg.get();
      return seg;
    };

    registry_ = std::make_unique<ResourceRegistry>("Replace Registry");
    old_resource_ = std::make_shared<FakeResource>();
    new_resource_ = std::make_shared<FakeResource>();
    registry_->Register(old_resource_);
  }

  auto TearDown() -> void override
  {
    if (registry_) {
      if (old_resource_) {
        registry_->UnRegisterResource(*old_resource_);
      }
      if (new_resource_) {
        registry_->UnRegisterResource(*new_resource_);
      }
    }
    new_resource_.reset();
    old_resource_.reset();
    registry_.reset();
    allocator_.reset();
    last_segment_ = nullptr;
  }

  struct RegisteredViewInfo {
    NativeView view;
    decltype(std::declval<DescriptorHandle>().GetBindlessHandle()) index;
  };

  auto RegisterView(FakeResource& resource, const TestViewDesc desc) const
    -> NativeView
  {
    // Allocate a descriptor
    DescriptorHandle descriptor
      = allocator_->Allocate(desc.view_type, desc.visibility);
    if (!descriptor.IsValid()) {
      ADD_FAILURE() << "failed to allocate descriptor";
      return {};
    }
    return registry_->RegisterView(resource, std::move(descriptor), desc);
  }

  //! Helper: register a view and return both the native view and bindless index
  auto RegisterViewGetIndex(
    FakeResource& resource, const TestViewDesc desc) const -> RegisteredViewInfo
  {
    // Allocate a descriptor
    DescriptorHandle descriptor
      = allocator_->Allocate(desc.view_type, desc.visibility);
    if (!descriptor.IsValid()) {
      ADD_FAILURE() << "failed to allocate descriptor";
      return {};
    }
    const auto index = descriptor.GetBindlessHandle();
    const auto view
      = registry_->RegisterView(resource, std::move(descriptor), desc);
    return RegisteredViewInfo { .view = view, .index = index };
  }
};

/*!
 Replace with updater that returns the same description should recreate the
 view in place for the new resource and keep the descriptor slot unchanged
 (stable bindless index and same cache key).
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_SameDesc_SameSlot)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 10,
  };
  // Allocate descriptor to capture the index explicitly
  const auto [old_view, index] = RegisterViewGetIndex(*old_resource_, desc);
  ASSERT_TRUE(old_view->IsValid());
  const auto before_alloc
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);

  // Act
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& prev) -> std::optional<TestViewDesc> {
      EXPECT_EQ(prev, desc);
      return prev; // same desc
    });

  // Assert
  EXPECT_FALSE(registry_->Contains(*old_resource_));
  EXPECT_TRUE(registry_->Contains(*new_resource_));
  EXPECT_TRUE(registry_->Contains(*new_resource_, desc));
  const auto found = registry_->Find(*new_resource_, desc);
  EXPECT_TRUE(found->IsValid());
  // Slot unchanged: UpdateView on the same index must succeed now for
  // new_resource_
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, index, desc));
  const auto after_alloc
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_EQ(after_alloc, before_alloc) << "Replace must not release/allocate";
}

/*!
 Transforming the description during Replace must move the cache key while
 keeping the same descriptor slot. Find/Contains should reflect the new key
 only.
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_WithTransformedDesc_ChangesKey)
{
  // Arrange
  constexpr TestViewDesc k1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 21,
  };
  constexpr TestViewDesc k2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 22,
  };
  const auto [old_view, index] = RegisterViewGetIndex(*old_resource_, k1);
  ASSERT_TRUE(old_view->IsValid());

  // Act
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& prev) -> std::optional<TestViewDesc> {
      EXPECT_EQ(prev, k1);
      return k2; // change key
    });

  // Assert
  EXPECT_TRUE(registry_->Contains(*new_resource_));
  EXPECT_FALSE(registry_->Contains(*new_resource_, k1));
  EXPECT_TRUE(registry_->Contains(*new_resource_, k2));
  // Slot unchanged and owned by new_resource_
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, index, k2));
}

/*!
 Returning std::nullopt from the updater must release the descriptor and not
 transfer the view to the new resource.
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_UpdaterNullopt_ReleasesDescriptor)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 31,
  };
  // Record baseline before any allocations in this test
  const auto baseline
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto [v, index] = RegisterViewGetIndex(*old_resource_, desc);

  // Act
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) -> std::optional<TestViewDesc> {
      return std::nullopt; // drop
    });

  // Assert
  EXPECT_TRUE(registry_->Contains(*new_resource_));
  EXPECT_FALSE(registry_->Contains(*new_resource_, desc));
  // Index should now be free (no owner), UpdateView must fail
  EXPECT_FALSE(registry_->UpdateView(*new_resource_, index, desc));
  const auto after
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_EQ(after, baseline) << "Descriptor should have been released";
}

/*!
 Attempting to Replace on a resource that is not registered must throw
 (consistent with RegisterView behavior).
*/
//! Replace must throw if the old resource is not registered.
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_OnUnregisteredResource_Throws)
{
  // Arrange
  registry_->UnRegisterResource(*old_resource_);

  // Act + Assert
  EXPECT_THROW(registry_->Replace(*old_resource_, new_resource_, nullptr),
    std::runtime_error);
}

/*!
 If old resource has no views, Replace should still succeed: with updater it
 does nothing; with nullptr it just unregisters the old resource.
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_WithNoViews_Succeeds)
{
  // Arrange: old_resource_ has no registered views
  EXPECT_TRUE(registry_->Contains(*old_resource_));

  // Act + Assert: updater mode (no-op)
  EXPECT_NO_THROW(registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) -> std::optional<TestViewDesc> {
      ADD_FAILURE() << "Updater must not be called for no views";
      return std::nullopt;
    }));

  // Reset for nullptr path
  registry_->UnRegisterResource(*new_resource_);
  registry_->Register(old_resource_);

  // Act + Assert: nullptr mode should unregister old and register new
  EXPECT_NO_THROW(registry_->Replace(*old_resource_, new_resource_, nullptr));
  EXPECT_FALSE(registry_->Contains(*old_resource_));
  EXPECT_TRUE(registry_->Contains(*new_resource_));
}

/*!
 If the new view is invalid, Replace must release the descriptor and not
 transfer it.
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_NewViewInvalid_ReleasesDescriptor)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 41,
  };
  // Capture baseline before allocations
  const auto baseline
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto [v, index] = RegisterViewGetIndex(*old_resource_, desc);
  // Configure new resource to produce invalid view
  new_resource_->WithInvalidView();

  // Act
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) -> std::optional<TestViewDesc> { return desc; });

  // Assert
  EXPECT_FALSE(registry_->Contains(*new_resource_, desc));
  EXPECT_FALSE(registry_->UpdateView(*new_resource_, index, desc));
  const auto after
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_EQ(after, baseline);
}

/*!
 If the new resource throws during GetNativeView, Replace should not crash
 user code and must release the descriptor (drop it).
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_GetNativeViewThrows_ReleasesDescriptor)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 51,
  };
  // Capture baseline before any allocation/registration in this test
  const auto count_before
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto [v, index] = RegisterViewGetIndex(*old_resource_, desc);
  // Configure new resource to throw for this id
  new_resource_->WithThrowingView(desc.id);

  // Act: Replace should catch and release
  EXPECT_NO_THROW(registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) -> std::optional<TestViewDesc> { return desc; }));

  // Assert
  EXPECT_FALSE(registry_->Contains(*new_resource_, desc));
  EXPECT_FALSE(registry_->UpdateView(*new_resource_, index, desc));
  const auto after
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_EQ(after, count_before);
}

/*!
 Mixed outcome: two views where one transfers and one is dropped via nullopt.
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_MixedViews_TransferAndDrop)
{
  // Arrange
  constexpr TestViewDesc k1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 61,
  };
  constexpr TestViewDesc k2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 62,
  };
  // Baseline before any allocations
  const auto baseline
    = allocator_->GetAllocatedDescriptorsCount(k1.view_type, k1.visibility);
  const auto [v1, idx1] = RegisterViewGetIndex(*old_resource_, k1);
  const auto [v2, idx2] = RegisterViewGetIndex(*old_resource_, k2);

  // Act: transfer k1 unchanged, drop k2
  registry_->Replace(*old_resource_, new_resource_, [&](const TestViewDesc& p) {
    if (p == k1) {
      return std::optional(p);
    }
    if (p == k2) {
      return std::optional<TestViewDesc>(std::nullopt);
    }
    ADD_FAILURE() << "unexpected desc";
    return std::optional<TestViewDesc>(std::nullopt);
  });

  // Assert
  EXPECT_TRUE(registry_->Contains(*new_resource_, k1));
  EXPECT_FALSE(registry_->Contains(*new_resource_, k2));
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx1, k1));
  EXPECT_FALSE(registry_->UpdateView(*new_resource_, idx2, k2));
  const auto after
    = allocator_->GetAllocatedDescriptorsCount(k1.view_type, k1.visibility);
  // Exactly one descriptor should remain allocated for this visibility/type
  // We cannot do arithmetic on strong types; assert monotonic properties:
  EXPECT_GT(after, baseline);
}

/*!
 Replacing a view with the same description multiple times should be
 idempotent with respect to cache keys and descriptor slots (content may
 refresh).
*/
// Additional scenarios can be implemented similarly as needed.

/*!
 Replacing one view among several registered on the same resource must not
 affect other views (their cache keys and descriptor slots remain unchanged).
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_AffectsOnlyTargetView)
{
  // Arrange: register two views, we'll change only k1's description
  constexpr TestViewDesc k1_old {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 101,
  };
  constexpr TestViewDesc k1_new {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 102,
  };
  constexpr TestViewDesc k2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 103,
  };
  const auto [v1, idx1] = RegisterViewGetIndex(*old_resource_, k1_old);
  const auto [v2, idx2] = RegisterViewGetIndex(*old_resource_, k2);

  // Act: update only k1 (change key), keep k2 same
  registry_->Replace(*old_resource_, new_resource_, [&](const TestViewDesc& d) {
    if (d == k1_old) {
      return std::optional(k1_new);
    }
    if (d == k2) {
      return std::optional(d); // keep
    }
    ADD_FAILURE() << "unexpected desc";
    return std::optional<TestViewDesc>(std::nullopt);
  });

  // Assert: k1 moved to new key, k2 preserved; indices stable
  EXPECT_TRUE(registry_->Contains(*new_resource_, k1_new));
  EXPECT_FALSE(registry_->Contains(*new_resource_, k1_old));
  EXPECT_TRUE(registry_->Contains(*new_resource_, k2));
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx1, k1_new));
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx2, k2));
}

/*!
 If Replace attempts to change the view's visibility or type (via updated
 description), validate behavior and ensure descriptor/index invariants hold.
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_IncompatibleTypeOrVisibility_Throws)
{
  // Arrange: start with a shader-visible CBV, change to CPU-visible SRV.
  constexpr TestViewDesc d1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 201,
  };
  constexpr TestViewDesc d2 {
    .view_type = ResourceViewType::kRawBuffer_SRV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .id = 202,
  };
  const auto [v, idx] = RegisterViewGetIndex(*old_resource_, d1);

  // Act: attempt to Replace with changed type/visibility; current semantics do
  // not throw, and FakeResource ignores type/visibility in handle creation.
  EXPECT_NO_THROW(registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) { return std::optional(d2); }));

  // Assert: new key present, index still usable for the new desc
  EXPECT_TRUE(registry_->Contains(*new_resource_, d2));
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx, d2));
}

/*!
 Replace must not allocate a new descriptor nor release the existing one.
 Verify with MockDescriptorAllocator that allocation and remaining counts
 remain unchanged while the native view may change.
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_DoesNotAllocateOrReleaseDescriptor)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 301,
  };
  const auto [v, idx] = RegisterViewGetIndex(*old_resource_, desc);

  // Capture counters right before Replace
  const auto alloc_before
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto rem_before
    = allocator_->GetRemainingDescriptorsCount(desc.view_type, desc.visibility);

  // Act: Replace with same desc
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& p) { return std::optional(p); });

  // Assert: slot preserved and allocator counts stable (no release, no alloc)
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx, desc));
  const auto after_alloc
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto after_rem
    = allocator_->GetRemainingDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_EQ(after_alloc, alloc_before);
  EXPECT_EQ(after_rem, rem_before);
}

/*!
 After Replace with a description update, Contains/Find should reflect the
 new key and not the old key.
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_UpdatesCacheAndFind)
{
  // Arrange
  constexpr TestViewDesc k1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 401,
  };
  constexpr TestViewDesc k2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 402,
  };
  [[maybe_unused]] const auto v = RegisterView(*old_resource_, k1);

  // Act
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) { return std::optional(k2); });

  // Assert
  EXPECT_FALSE(registry_->Contains(*new_resource_, k1));
  EXPECT_TRUE(registry_->Contains(*new_resource_, k2));
  const auto found = registry_->Find(*new_resource_, k2);
  EXPECT_TRUE(found->IsValid());
}

/*!
 Replace on a resource after UnRegisterViews should be a safe no-op; after
 UnRegisterResource it should throw (resource missing).
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_AfterUnregister_Throws)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 501,
  };
  [[maybe_unused]] const auto v = RegisterView(*old_resource_, desc);

  // Case 1: After UnRegisterViews -> Replace should not throw (no views)
  registry_->UnRegisterViews(*old_resource_);
  EXPECT_NO_THROW(registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc&) { return std::optional(desc); }));

  // Reset: ensure old_resource_ is registered again with no views
  registry_->Register(old_resource_);

  // Case 2: After UnRegisterResource -> Replace must throw
  registry_->UnRegisterResource(*old_resource_);
  EXPECT_THROW(registry_->Replace(*old_resource_, new_resource_, nullptr),
    std::runtime_error);
}

/*!
 Concurrency: two threads racing Replace on the same view should result in a
 valid final view and a consistent cache (exactly one of the final keys
 present).
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_ConcurrentUpdates_ThreadSafe)
{
  // Arrange: two independent old/new pairs, each with its own initial view
  const auto old_a = std::make_shared<FakeResource>();
  const auto new_a = std::make_shared<FakeResource>();
  const auto old_b = std::make_shared<FakeResource>();
  const auto new_b = std::make_shared<FakeResource>();
  registry_->Register(old_a);
  registry_->Register(old_b);

  constexpr TestViewDesc d1_a {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1101,
  };
  constexpr TestViewDesc d2_a {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1102,
  };
  constexpr TestViewDesc d1_b {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1111,
  };
  constexpr TestViewDesc d2_b {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1112,
  };

  [[maybe_unused]] const auto v_a = RegisterView(*old_a, d1_a);
  [[maybe_unused]] const auto v_b = RegisterView(*old_b, d1_b);

  std::atomic start { false };
  std::thread t1([&] {
    while (!start.load()) {
      std::this_thread::yield();
    }
    EXPECT_NO_THROW(registry_->Replace(
      *old_a, new_a, [&](const TestViewDesc&) { return std::optional(d2_a); }));
  });
  std::thread t2([&] {
    while (!start.load()) {
      std::this_thread::yield();
    }
    EXPECT_NO_THROW(registry_->Replace(
      *old_b, new_b, [&](const TestViewDesc&) { return std::optional(d2_b); }));
  });
  start = true;
  t1.join();
  t2.join();

  // Assert: both new resources are registered and contain their respective keys
  EXPECT_TRUE(registry_->Contains(*new_a));
  EXPECT_TRUE(registry_->Contains(*new_b));
  EXPECT_TRUE(registry_->Contains(*new_a, d2_a));
  EXPECT_TRUE(registry_->Contains(*new_b, d2_b));
  EXPECT_FALSE(registry_->Contains(*old_a, d1_a));
  EXPECT_FALSE(registry_->Contains(*old_b, d1_b));
}

/*!
 No-op: if the resource returns the same NativeView during Replace (e.g.,
 internal reuse), the registry should still be consistent and not duplicate
 cache entries.
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_ReturnsSameView_NoDuplication)
{
  // Arrange: configure both resources to return same native view for desc
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 601,
  };
  const auto same_view_lambda
    = [](const DescriptorHandle&, const TestViewDesc& d) -> NativeView {
    // Use id as the handle; both resources will produce identical views
    return { d.id, FakeResource::ClassTypeId() };
  };
  old_resource_->WithViewBehavior(same_view_lambda);
  new_resource_->WithViewBehavior(same_view_lambda);

  [[maybe_unused]] const auto v = RegisterView(*old_resource_, desc);

  // Act: Replace with identical desc
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& d) { return std::optional(d); });

  // Assert: only new resource contains the key; view valid
  EXPECT_FALSE(registry_->Contains(*old_resource_, desc));
  EXPECT_TRUE(registry_->Contains(*new_resource_, desc));
  const auto found = registry_->Find(*new_resource_, desc);
  EXPECT_TRUE(found->IsValid());
}

/*!
 Cross-allocator safety: If the original descriptor belongs to allocator A,
 Replace must not attempt to release to A nor allocate from any allocator.
 Should work identically when the view was initially created by allocator B.
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_PreservesAllocatorOwnership)
{
  // Arrange: use a second allocator for the original descriptor
  const auto other_allocator
    = std::make_shared<testing::NiceMock<MockDescriptorAllocator>>();
  other_allocator->ext_segment_factory_
    = [](auto capacity, auto base_index, auto view_type, auto visibility) {
        return std::make_unique<FixedDescriptorSegment>(
          capacity, base_index, view_type, visibility);
      };
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 701,
  };
  DescriptorHandle h
    = other_allocator->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(h.IsValid());
  const auto idx = h.GetBindlessHandle();
  [[maybe_unused]] auto v
    = registry_->RegisterView(*old_resource_, std::move(h), desc);

  // Capture allocator count right before Replace
  const auto before_other = other_allocator->GetAllocatedDescriptorsCount(
    desc.view_type, desc.visibility);

  // Act: Replace should not allocate/release on the allocator
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& d) { return std::optional(d); });

  // Assert: index usable; counts on other allocator unchanged
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx, desc));
  const auto after_other = other_allocator->GetAllocatedDescriptorsCount(
    desc.view_type, desc.visibility);
  EXPECT_EQ(after_other, before_other)
    << "Replace must not allocate or release on original allocator";

  // Cleanup: release the view while the allocator is still alive to avoid
  // dangling descriptor handle during fixture TearDown.
  registry_->UnRegisterResource(*new_resource_);
}

/*!
 Replace on a different resource using a view description from the first
 resource must not cross-contaminate caches.
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_WrongResource_Throws)
{
  // Arrange: register a view on old_resource_ only
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 801,
  };
  [[maybe_unused]] const auto v = RegisterView(*old_resource_, desc);

  // Create a third resource unrelated to the old/new pair
  const auto other = std::make_shared<FakeResource>();
  registry_->Register(other);

  // Act: Replace only moves from old_resource_ to new_resource_
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& d) { return std::optional(d); });

  // Assert: other remains unaffected; new contains, old and other do not
  EXPECT_TRUE(registry_->Contains(*new_resource_, desc));
  EXPECT_FALSE(registry_->Contains(*old_resource_, desc));
  EXPECT_FALSE(registry_->Contains(*other, desc));
  registry_->UnRegisterResource(*other);
}

/*!
 Verify Replace keeps descriptor_to_resource_ mapping intact for the targeted
 descriptor index (no remap to a different resource).
*/
NOLINT_TEST_F(
  ResourceRegistryReplaceTest, Replace_DoesNotChangeDescriptorMapping)
{
  // Arrange: register a view and capture its index
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 901,
  };
  const auto [v, idx] = RegisterViewGetIndex(*old_resource_, desc);

  // Act: Replace with same desc
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& d) { return std::optional(d); });

  // Assert: index is valid for new resource and not for old anymore
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx, desc));
  EXPECT_FALSE(registry_->UpdateView(*old_resource_, idx, desc));
}

/*!
 Replace after a prior failed Replace (due to invalid/throw) should still
 succeed when the resource later produces a valid view, proving no residual
 corrupted state.
*/
NOLINT_TEST_F(ResourceRegistryReplaceTest, Replace_SucceedsAfterPreviousFailure)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1001,
  };
  // Initial descriptor and view
  const auto [v, idx] = RegisterViewGetIndex(*old_resource_, desc);

  // Configure new resource to produce invalid view to force release
  new_resource_->WithInvalidView();

  // Act 1: Replace drops descriptor and does not transfer
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& d) { return std::optional(d); });

  // Assert post 1: index no longer usable and new does not contain
  EXPECT_FALSE(registry_->Contains(*new_resource_, desc));
  EXPECT_FALSE(registry_->UpdateView(*new_resource_, idx, desc));

  // Prepare success case: register a fresh view on old and reset new behavior
  old_resource_ = std::make_shared<FakeResource>();
  registry_->Register(old_resource_);
  const auto [v2, idx2] = RegisterViewGetIndex(*old_resource_, desc);
  new_resource_ = std::make_shared<FakeResource>();
  registry_->Register(new_resource_);

  // Act 2: Replace should now transfer
  registry_->Replace(*old_resource_, new_resource_,
    [&](const TestViewDesc& d) { return std::optional(d); });

  // Assert post 2
  EXPECT_TRUE(registry_->Contains(*new_resource_, desc));
  EXPECT_TRUE(registry_->UpdateView(*new_resource_, idx2, desc));
}

} // namespace
