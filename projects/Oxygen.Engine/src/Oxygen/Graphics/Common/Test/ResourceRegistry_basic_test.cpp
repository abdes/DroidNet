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
// Basic Tests
//===----------------------------------------------------------------------===//

// ReSharper disable once CppRedundantQualifier
class ResourceRegistryBasicTest : public ::testing::Test {
protected:
  std::shared_ptr<MockDescriptorAllocator> allocator_;
  FixedDescriptorSegment* last_segment_ { nullptr };

  std::unique_ptr<ResourceRegistry> registry_;

  std::shared_ptr<FakeResource> resource1_;
  std::shared_ptr<FakeResource> resource2_;

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

    registry_ = std::make_unique<ResourceRegistry>("Test Registry");
    resource1_ = std::make_shared<FakeResource>();
    resource2_ = std::make_shared<FakeResource>();
    registry_->Register(resource1_);
    registry_->Register(resource2_);
  }

  auto TearDown() -> void override
  {
    if (registry_) {
      if (resource1_) {
        registry_->UnRegisterResource(*resource1_);
      }
      if (resource2_) {
        registry_->UnRegisterResource(*resource2_);
      }
    }
    resource1_.reset();
    resource2_.reset();
    registry_.reset();
    allocator_.reset();
  }

  auto RegisterView(FakeResource& resource, TestViewDesc desc) const
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
};

/*!
 Verify that a resource registered in SetUp() is present in the registry. The
 test asserts ResourceRegistry::Contains returns true for the resource.
*/
NOLINT_TEST_F(ResourceRegistryBasicTest, Register_ContainsResource)
{
  EXPECT_TRUE(registry_->Contains(*resource1_));
}

//! Verify UnRegisterResource is idempotent: calling it multiple times is safe.
NOLINT_TEST_F(ResourceRegistryBasicTest, UnRegister_Idempotent)
{
  // Ensure resource1_ is present
  EXPECT_TRUE(registry_->Contains(*resource1_));
  // First unregister should remove the resource
  registry_->UnRegisterResource(*resource1_);
  EXPECT_FALSE(registry_->Contains(*resource1_));
  // Second unregister should be a no-op and not throw; state remains absent
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterResource(*resource1_));
  EXPECT_FALSE(registry_->Contains(*resource1_));
}

/*!
 Given two registered views on the same resource, UnRegisterView should remove
 only the specified view and leave the other view and the resource registration
 intact.
*/
NOLINT_TEST_F(ResourceRegistryBasicTest, UnRegisterView_RemovesOnlyTarget)
{
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2,
  };
  const auto view1 = RegisterView(*resource1_, desc1);
  (void)RegisterView(*resource1_, desc2);

  EXPECT_TRUE(registry_->Contains(*resource1_, desc1));
  EXPECT_TRUE(registry_->Contains(*resource1_, desc2));

  registry_->UnRegisterView(*resource1_, view1);

  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_TRUE(registry_->Contains(*resource1_, desc2));
}

/*!
 UnRegisterView with an invalid or non-existent native view must be a safe no-op
 and must not throw exceptions.
*/
NOLINT_TEST_F(ResourceRegistryBasicTest, UnRegisterView_InvalidView_NoThrow)
{
  constexpr NativeView invalid_view {};
  EXPECT_FALSE(invalid_view->IsValid());
  // Unregistering a non-existent view should not throw
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterView(*resource1_, invalid_view));
}

/*!
 Basic lifecycle: with two registered resources, unregister one at a time and
 verify the registry reflects presence/absence accordingly.
*/
NOLINT_TEST_F(
  ResourceRegistryBasicTest, RegisterUnregister_MultipleResources_ReflectsState)
{
  EXPECT_TRUE(registry_->Contains(*resource1_));
  EXPECT_TRUE(registry_->Contains(*resource2_));
  registry_->UnRegisterResource(*resource1_);
  EXPECT_FALSE(registry_->Contains(*resource1_));
  EXPECT_TRUE(registry_->Contains(*resource2_));
  registry_->UnRegisterResource(*resource2_);
  EXPECT_FALSE(registry_->Contains(*resource2_));
}

/*!
 UnRegisterViews must remove all views for a resource.
*/
NOLINT_TEST_F(ResourceRegistryBasicTest, UnRegisterViews_RemovesAllViews)
{
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2,
  };
  [[maybe_unused]] auto view1 = RegisterView(*resource1_, desc1);
  [[maybe_unused]] auto view2 = RegisterView(*resource1_, desc2);

  EXPECT_TRUE(registry_->Contains(*resource1_, desc1));
  EXPECT_TRUE(registry_->Contains(*resource1_, desc2));

  registry_->UnRegisterViews(*resource1_);

  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_FALSE(registry_->Contains(*resource1_, desc2));
}

/*!
 UnRegisterViews should not remove the resource itself. After calling it, the
 resource must still be reported as present, but view containment must be false
 for the removed view descriptions.
*/
NOLINT_TEST_F(ResourceRegistryBasicTest, UnRegisterViews_DoesNotRemoveResource)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 5,
  };
  RegisterView(*resource1_, desc);
  EXPECT_TRUE(registry_->Contains(*resource1_));

  registry_->UnRegisterViews(*resource1_);

  EXPECT_TRUE(registry_->Contains(*resource1_));
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
}

/*!
 Ensure RegisterView's returned view can be found via Find() and that Find
 returns the identical native view object.
*/
NOLINT_TEST_F(
  ResourceRegistryBasicTest, RegisterView_Find_ReturnsSameNativeView)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 200,
  };
  const auto view = RegisterView(*resource1_, desc);
  ASSERT_TRUE(view->IsValid());
  const auto found = registry_->Find(*resource1_, desc);
  EXPECT_TRUE(found->IsValid());
  EXPECT_EQ(found, view);
}

/*!
 Verify that UnRegisterViews actually releases descriptor slots back to the
 allocator (no descriptor leak).
*/
NOLINT_TEST_F(ResourceRegistryBasicTest, UnRegisterViews_ReleasesDescriptors)
{
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 301,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 302,
  };
  // Record allocated count before
  const auto before = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  [[maybe_unused]] auto v1 = RegisterView(*resource1_, desc1);
  [[maybe_unused]] auto v2 = RegisterView(*resource1_, desc2);
  // Sanity: we allocated descriptors
  const auto after_alloc = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  EXPECT_GT(after_alloc, before);

  registry_->UnRegisterViews(*resource1_);

  const auto after_release = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  EXPECT_EQ(after_release, before);
}

//===----------------------------------------------------------------------===//
// Error Handling Tests
//===----------------------------------------------------------------------===//

class ResourceRegistryErrorTest : public ResourceRegistryBasicTest { };

/*!
 Attempting to register a view on a resource that was never registered in the
 registry must fail gracefully by returning an invalid NativeObject.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, RegisterView_UnregisteredResource_ReturnsInvalid)
{
  const auto unregistered_resource = std::make_shared<FakeResource>();
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 99,
  };
  const auto view_object = RegisterView(*unregistered_resource, desc);
  EXPECT_FALSE(view_object->IsValid());
}

/*!
 Finding or checking containment for a view on an unregistered resource must
 return false/invalid. Verifies safe behavior for unknown resources.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, Find_UnregisteredResource_ReturnsInvalid)
{
  const auto unregistered_resource = std::make_shared<FakeResource>();
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 100,
  };
  EXPECT_FALSE(registry_->Contains(*unregistered_resource, desc));
  EXPECT_FALSE(registry_->Find(*unregistered_resource, desc)->IsValid());
}

/*!
 Death test: RegisterView must abort when given an invalid descriptor handle.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterView_InvalidHandle_Death)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 101,
  };
  DescriptorHandle invalid_handle; // default constructed, invalid
  NOLINT_EXPECT_DEATH(
    registry_->RegisterView(*resource1_, std::move(invalid_handle), desc),
    ".*");
}

/*!
 Registering the same resource twice must throw.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, Register_DoubleRegister_Death)
{
  // Registering the same resource twice should throw
  EXPECT_TRUE(registry_->Contains(*resource1_));
  NOLINT_EXPECT_DEATH(registry_->Register(resource1_), ".*");
}

/*!
 Registering the same view twice should abort.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterView_DoubleRegister_Death)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 101,
  };
  // Registering the same resource twice should throw
  EXPECT_TRUE(registry_->Contains(*resource1_));
  auto view = RegisterView(*resource1_, desc);
  EXPECT_TRUE(view->IsValid());
  NOLINT_EXPECT_DEATH((void)RegisterView(*resource1_, desc), ".*");
}

/*!
 Registering a null resource aborts.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, Register_NullResource_Death)
{
  const std::shared_ptr<FakeResource> null_res;
  EXPECT_DEATH(registry_->Register(null_res), ".*");
}

/*!
 Registering a view with an invalid native view object (but valid handle) should
 return false without throwing or aborting.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, RegisterView_InvalidNativeView_ReturnsFalse)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 202,
  };
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  const NativeView invalid_view; // default constructed, invalid
  // Should return false (not throw or abort)
  const bool result = registry_->RegisterView(
    *resource1_, invalid_view, std::move(descriptor), desc);
  EXPECT_FALSE(result);
}

/*!
 Registering a view with an invalid native view object should not leak the
 descriptor: the descriptor must be released by the registry on failure.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, RegisterView_InvalidNativeView_ReleasesDescriptor)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 303,
  };

  const auto before
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());

  const NativeView invalid_view; // default constructed invalid
  const bool result = registry_->RegisterView(
    *resource1_, invalid_view, std::move(descriptor), desc);
  EXPECT_FALSE(result);

  const auto after
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_EQ(after, before);
}

/*!
 Verify descriptors allocated from a different allocator are released back to
 their allocator when views are unregistered.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, RegisterView_DifferentAllocator_ReleasesToOrigin)
{
  // Create a second allocator instance with same segment factory behavior
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
    .id = 404,
  };

  const auto before = other_allocator->GetAllocatedDescriptorsCount(
    desc.view_type, desc.visibility);
  DescriptorHandle descriptor
    = other_allocator->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());

  // Register view using descriptor from other allocator
  const auto view
    = registry_->RegisterView(*resource1_, std::move(descriptor), desc);
  ASSERT_TRUE(view->IsValid());

  // Unregister the specific view which should release its descriptor back to
  // the allocator that created it (other_allocator)
  registry_->UnRegisterView(*resource1_, view);

  const auto after = other_allocator->GetAllocatedDescriptorsCount(
    desc.view_type, desc.visibility);
  EXPECT_EQ(after, before);
}

/*!
 UnRegisterView called for a resource that was never registered or was already
 unregistered must throw.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, UnRegisterView_ForUnregisteredResource_Throws)
{
  const auto unregistered_resource = std::make_shared<FakeResource>();
  // Create a valid view object via the fake resource using a temporary
  // DescriptorHandle from the allocator so we have a plausible view to pass.
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 401,
  };
  const DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());
  const auto view = unregistered_resource->GetNativeView(descriptor, desc);
  EXPECT_THROW(registry_->UnRegisterView(*unregistered_resource, view),
    std::runtime_error);
}

/*!
 Calling UnRegisterViews on an unregistered resource should be a safe no-op.
*/
NOLINT_TEST_F(
  ResourceRegistryErrorTest, UnRegisterViews_OnUnregisteredResource_NoThrow)
{
  const auto unregistered_resource = std::make_shared<FakeResource>();
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterViews(*unregistered_resource));
  EXPECT_FALSE(registry_->Contains(*unregistered_resource));
}

//===----------------------------------------------------------------------===//
// View Caching and Uniqueness Tests
//===----------------------------------------------------------------------===//

class ResourceRegistryViewCacheTest : public ResourceRegistryBasicTest { };

/*!
 Two distinct view descriptions for the same resource must produce two distinct
 native views and both should be valid and present in the cache.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest,
  RegisterView_DifferentDescriptions_CreateDistinctViews)
{
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2,
  };
  const auto view1 = RegisterView(*resource1_, desc1);
  const auto view2 = RegisterView(*resource1_, desc2);
  EXPECT_TRUE(view1->IsValid());
  EXPECT_TRUE(view2->IsValid());
  EXPECT_NE(view1, view2)
    << "Different descriptions should yield different views";
}

/*!
 After UnRegisterViews on a resource, cached entries for that resource are
 purged. Registering a view with the same description on a different (new)
 resource must yield a different view, proving no stale cache reuse.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest,
  RegisterView_CacheEviction_AfterUnregisterViews)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 77,
  };
  const auto view1 = RegisterView(*resource1_, desc);
  registry_->UnRegisterViews(*resource1_);
  // Allocate a new resource to guarantee a new pointer
  const auto view2 = RegisterView(*resource2_, desc);
  EXPECT_TRUE(view2->IsValid());
  EXPECT_NE(view1, view2)
    << "Cache should be cleared after UnRegisterViews, "
       "new view should be created for new resource instance";
  // Contract: UnRegisterResource must remove views for the resource. Assert it.
  registry_->UnRegisterResource(*resource2_);
  EXPECT_FALSE(registry_->Contains(*resource2_, desc));
}

/*!
 Registering the same view description on two different resources must create
 two distinct native views. The cache is keyed per resource.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest,
  RegisterView_MultipleResources_IndependentViews)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 123,
  };
  const auto view1 = RegisterView(*resource1_, desc);
  const auto view2 = RegisterView(*resource2_, desc);
  EXPECT_TRUE(view1->IsValid());
  EXPECT_TRUE(view2->IsValid());
  EXPECT_NE(view1, view2)
    << "Same description on different resources should yield different views";

  // Cleanup
  registry_->UnRegisterResource(*resource2_);
  EXPECT_FALSE(registry_->Contains(*resource2_, desc));
}

/*!
 After UnRegisterResource, re-registering a new instance of the resource and
 registering the same view description must produce a new native view (no stale
 view is returned across resource lifetimes).
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest,
  RegisterView_AfterUnRegisterResource_YieldsNewView)
{
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 55,
  };
  const auto view1 = RegisterView(*resource1_, desc);
  registry_->UnRegisterResource(*resource1_);
  // Allocate a new resource to guarantee a new pointer
  resource1_ = std::make_shared<FakeResource>();
  registry_->Register(resource1_);
  const auto view2 = RegisterView(*resource1_, desc);
  EXPECT_TRUE(view2->IsValid());
  EXPECT_NE(view1, view2) << "Re-registering with a new resource instance "
                             "should not return stale view";
}

//===----------------------------------------------------------------------===//
//  Concurrency Tests
//===----------------------------------------------------------------------===//

class ResourceRegistryConcurrencyTest : public ResourceRegistryBasicTest { };

/*!
 Stress test: multiple threads repeatedly register a resource, register a view,
 and unregister the resource. Verifies thread safety of registry data structures
 and absence of races or crashes under contention.
*/
NOLINT_TEST_F(
  ResourceRegistryConcurrencyTest, Concurrent_RegisterAndUnregister_Smoke)
{
  constexpr int num_threads = 8;
  constexpr int num_iterations = 100;
  std::atomic start_flag { false };
  std::vector<std::thread> threads;
  std::vector<std::shared_ptr<FakeResource>> resources(num_threads);
  // Give each thread its own allocator to isolate allocator effects while
  // sharing the same ResourceRegistry instance to test registry concurrency.
  std::vector<std::shared_ptr<MockDescriptorAllocator>> allocators(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    resources[i] = std::make_shared<FakeResource>();
    allocators[i]
      = std::make_shared<testing::NiceMock<MockDescriptorAllocator>>();
    allocators[i]->ext_segment_factory_
      = [](auto capacity, auto base_index, auto view_type, auto visibility) {
          return std::make_unique<FixedDescriptorSegment>(
            capacity, base_index, view_type, visibility);
        };
  }
  threads.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      while (!start_flag.load()) {
        std::this_thread::yield();
      }
      for (int i = 0; i < num_iterations; ++i) {
        // Register/unregister against the shared registry
        registry_->Register(resources[t]);
        TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
          .visibility = DescriptorVisibility::kShaderVisible,
          .id = static_cast<uint64_t>(i) };
        // Allocate descriptor using thread-local allocator to avoid contention
        DescriptorHandle descriptor
          = allocators[t]->Allocate(desc.view_type, desc.visibility);
        if (!descriptor.IsValid()) {
          ADD_FAILURE() << "failed to allocate descriptor in thread";
          continue;
        }
        auto view
          = registry_->RegisterView(*resources[t], std::move(descriptor), desc);
        EXPECT_TRUE(view->IsValid());
        registry_->UnRegisterResource(*resources[t]);
      }
    });
  }
  start_flag = true;
  for (auto& th : threads) {
    th.join();
  }
}

} // namespace
