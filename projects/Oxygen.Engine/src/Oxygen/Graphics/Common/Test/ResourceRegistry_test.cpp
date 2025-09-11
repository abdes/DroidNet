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

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeObject;
using oxygen::graphics::RegisteredResource;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::FixedDescriptorSegment;

using oxygen::graphics::bindless::testing::MockDescriptorAllocator;

namespace {

// Minimal test resource and view description
struct TestViewDesc {
  ResourceViewType view_type { ResourceViewType::kConstantBuffer };
  DescriptorVisibility visibility { DescriptorVisibility::kShaderVisible };
  int id { 0 };

  // Required for hash and equality comparison
  // NOLINTNEXTLINE(*-unneeded-member-function)
  auto operator==(const TestViewDesc& other) const -> bool
  {
    return id == other.id && view_type == other.view_type
      && visibility == other.visibility;
  }
};

class TestResource final : public RegisteredResource, public oxygen::Object {
  OXYGEN_TYPED(TestResource)
public:
  using ViewDescriptionT = TestViewDesc;

  // Required by resource registry
  [[maybe_unused]] auto GetNativeView(const DescriptorHandle& /*view_handle*/,
    const ViewDescriptionT& desc) -> NativeObject
  {
    // Use the resource pointer and view id to make each view unique per
    // resource and description
    const uint64_t ptr = reinterpret_cast<uint64_t>(this);
    const uint64_t id = static_cast<uint64_t>(desc.id);
    return { (ptr << 16) | (id & 0xFFFF), ClassTypeId() };
  }
};

// A resource that can throw from GetNativeView for a specific description id
class ThrowingTestResource final : public RegisteredResource,
                                   public oxygen::Object {
  OXYGEN_TYPED(ThrowingTestResource)
public:
  using ViewDescriptionT = TestViewDesc;

  void SetThrowOnId(std::optional<int> id) { throw_on_id_ = id; }

  [[maybe_unused]] auto GetNativeView(const DescriptorHandle& /*view_handle*/,
    const ViewDescriptionT& desc) -> NativeObject
  {
    if (throw_on_id_.has_value() && desc.id == *throw_on_id_) {
      throw std::runtime_error("ThrowingTestResource: GetNativeView fail");
    }
    const uint64_t ptr = reinterpret_cast<uint64_t>(this);
    const uint64_t id = static_cast<uint64_t>(desc.id);
    return { (ptr << 16) | (id & 0xFFFF), ClassTypeId() };
  }

private:
  std::optional<int> throw_on_id_ {};
};

// A resource that always returns an invalid native view (to simulate failures)
class InvalidViewTestResource final : public RegisteredResource,
                                      public oxygen::Object {
  OXYGEN_TYPED(InvalidViewTestResource)
public:
  using ViewDescriptionT = TestViewDesc;

  [[maybe_unused]] auto GetNativeView(const DescriptorHandle& /*view_handle*/,
    const ViewDescriptionT& /*desc*/) -> NativeObject
  {
    return {}; // invalid view
  }
};
} // namespace

template <> struct std::hash<TestViewDesc> {
  auto operator()(const TestViewDesc& v) const noexcept -> std::size_t
  {
    std::size_t h = std::hash<int> {}(static_cast<int>(v.view_type));
    oxygen::HashCombine(h, static_cast<int>(v.visibility));
    oxygen::HashCombine(h, v.id);
    return h;
  }
};

namespace {

class ResourceRegistryTest : public ::testing::Test {
protected:
  std::shared_ptr<MockDescriptorAllocator> allocator_;
  std::unique_ptr<ResourceRegistry> registry_;
  std::shared_ptr<TestResource> resource_;

  void SetUp() override
  {
    allocator_
      = std::make_shared<::testing::NiceMock<MockDescriptorAllocator>>();
    allocator_->ext_segment_factory_
      = [](auto capacity, auto base_index, auto view_type, auto visibility) {
          return std::make_unique<FixedDescriptorSegment>(
            capacity, base_index, view_type, visibility);
        };

    registry_ = std::make_unique<ResourceRegistry>("Test Registry");
    resource_ = std::make_shared<TestResource>();
    registry_->Register(resource_);
  }

  void TearDown() override
  {
    registry_->UnRegisterResource(*resource_);
    resource_.reset();
    registry_.reset();
    allocator_.reset();
  }

  auto RegisterView(TestResource& resource, TestViewDesc desc) -> NativeObject
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

// --- Resource Registration Tests ---
class ResourceRegistryRegistrationTest : public ResourceRegistryTest { };

/*!
 Verify that a resource registered in SetUp() is present in the registry. The
 test asserts ResourceRegistry::Contains returns true for the resource.
*/
NOLINT_TEST_F(ResourceRegistryRegistrationTest, RegisterAndContains)
{
  EXPECT_TRUE(registry_->Contains(*resource_));
}

/*!
 Registering the same resource twice must throw; after unregistering, the
 resource should no longer be present. Repeated UnRegisterResource on an
 already-removed resource should be a no-op and must not throw.
*/
NOLINT_TEST_F(ResourceRegistryRegistrationTest, DoubleRegisterAndUnregister)
{
  // Registering the same resource twice should throw
  EXPECT_THROW(registry_->Register(resource_), std::runtime_error);
  EXPECT_TRUE(registry_->Contains(*resource_));

  registry_->UnRegisterResource(*resource_);
  EXPECT_FALSE(registry_->Contains(*resource_));

  // Unregistering again should not throw, just be a no-op
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterResource(*resource_));
  EXPECT_FALSE(registry_->Contains(*resource_));
}

// --- View Caching and Uniqueness Tests ---
class ResourceRegistryViewCacheTest : public ResourceRegistryTest { };

/*!
 Registering a view for a resource and the same description twice must trigger a
 uniqueness violation. The second registration is expected to throw, proving
 per-resource, per-description uniqueness in the cache.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewAlreadyRegistered)
{
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 42 };
  const auto view1 = RegisterView(*resource_, desc);
  EXPECT_TRUE(view1.IsValid());
  EXPECT_TRUE(registry_->Contains(*resource_, desc));

  // Registering the same view again should throw a runtime error
  EXPECT_THROW(RegisterView(*resource_, desc), std::runtime_error)
    << "Registering the same view again should throw a runtime error";
}

/*!
 Two distinct view descriptions for the same resource must produce two distinct
 native views and both should be valid and present in the cache.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewDifferentDescriptions)
{
  constexpr TestViewDesc desc1 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1 };
  constexpr TestViewDesc desc2 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2 };
  const auto view1 = RegisterView(*resource_, desc1);
  const auto view2 = RegisterView(*resource_, desc2);
  EXPECT_TRUE(view1.IsValid());
  EXPECT_TRUE(view2.IsValid());
  EXPECT_NE(view1, view2)
    << "Different descriptions should yield different views";
}

/*!
 After UnRegisterViews on a resource, cached entries for that resource are
 purged. Registering a view with the same description on a different (new)
 resource must yield a different view, proving no stale cache reuse.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewCacheEviction)
{
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 77 };
  const auto view1 = RegisterView(*resource_, desc);
  registry_->UnRegisterViews(*resource_);
  // Allocate a new resource to guarantee a new pointer
  const auto resource2 = std::make_shared<TestResource>();
  registry_->Register(resource2);
  const auto view2 = RegisterView(*resource2, desc);
  EXPECT_TRUE(view2.IsValid());
  EXPECT_NE(view1, view2)
    << "Cache should be cleared after UnRegisterViews, "
       "new view should be created for new resource instance";

  // Cleanup
  registry_->UnRegisterResource(*resource2);
}

/*!
 Registering the same view description on two different resources must create
 two distinct native views. The cache is keyed per resource.
*/
NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewMultipleResources)
{
  const auto resource2 = std::make_shared<TestResource>();
  registry_->Register(resource2);
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 123 };
  const auto view1 = RegisterView(*resource_, desc);
  const auto view2 = RegisterView(*resource2, desc);
  EXPECT_TRUE(view1.IsValid());
  EXPECT_TRUE(view2.IsValid());
  EXPECT_NE(view1, view2)
    << "Same description on different resources should yield different views";

  // Cleanup
  registry_->UnRegisterResource(*resource2);
}

/*!
 After UnRegisterResource, re-registering a new instance of the resource and
 registering the same view description must produce a new native view (no stale
 view is returned across resource lifetimes).
*/
NOLINT_TEST_F(
  ResourceRegistryViewCacheTest, RegisterViewAfterUnregisterResource)
{
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 55 };
  const auto view1 = RegisterView(*resource_, desc);
  registry_->UnRegisterResource(*resource_);
  // Allocate a new resource to guarantee a new pointer
  resource_ = std::make_shared<TestResource>();
  registry_->Register(resource_);
  const auto view2 = RegisterView(*resource_, desc);
  EXPECT_TRUE(view2.IsValid());
  EXPECT_NE(view1, view2) << "Re-registering with a new resource instance "
                             "should not return stale view";
}

// --- Error Handling Tests ---
class ResourceRegistryErrorTest : public ResourceRegistryTest { };

/*!
 Attempting to register a view on a resource that was never registered in the
 registry must fail gracefully by returning an invalid NativeObject.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterViewForUnregisteredResource)
{
  const auto unregistered_resource = std::make_shared<TestResource>();
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 99 };
  const auto view_object = RegisterView(*unregistered_resource, desc);
  EXPECT_FALSE(view_object.IsValid());
}

/*!
 Finding or checking containment for a view on an unregistered resource must
 return false/invalid. Verifies safe behavior for unknown resources.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, FindViewForUnregisteredResource)
{
  const auto unregistered_resource = std::make_shared<TestResource>();
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 100 };
  EXPECT_FALSE(registry_->Contains(*unregistered_resource, desc));
  EXPECT_FALSE(registry_->Find(*unregistered_resource, desc).IsValid());
}

/*!
 Death test: RegisterView must abort when given an invalid descriptor handle.
 Ensures descriptor preconditions are enforced in the public API.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterViewWithInvalidHandle)
{
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 101 };
  DescriptorHandle invalid_handle; // default constructed, invalid
  EXPECT_DEATH(
    registry_->RegisterView(*resource_, std::move(invalid_handle), desc), ".*");
}

/*!
 Registering a view with an invalid native view object (but valid handle) should
 return false without throwing or aborting.
*/
NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterViewWithInvalidView)
{
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 202 };
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  NativeObject invalid_view; // default constructed, invalid
  // Should return false (not throw or abort)
  bool result = registry_->RegisterView(
    *resource_, invalid_view, std::move(descriptor), desc);
  EXPECT_FALSE(result);
}

// --- Concurrency Tests ---
class ResourceRegistryConcurrencyTest : public ResourceRegistryTest { };

/*!
 Stress test: multiple threads repeatedly register a resource, register a view,
 and unregister the resource. Verifies thread safety of registry data structures
 and absence of races or crashes under contention.
*/
NOLINT_TEST_F(ResourceRegistryConcurrencyTest, ConcurrentRegisterAndUnregister)
{
  constexpr int kNumThreads = 8;
  constexpr int kNumIterations = 100;
  std::atomic start_flag { false };
  std::vector<std::thread> threads;
  std::vector<std::shared_ptr<TestResource>> resources(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    resources[i] = std::make_shared<TestResource>();
  }
  threads.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t]() {
      while (!start_flag.load()) {
        std::this_thread::yield();
      }
      for (int i = 0; i < kNumIterations; ++i) {
        registry_->Register(resources[t]);
        TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
          .visibility = DescriptorVisibility::kShaderVisible,
          .id = i };
        auto view = RegisterView(*resources[t], desc);
        EXPECT_TRUE(view.IsValid());
        registry_->UnRegisterResource(*resources[t]);
      }
    });
  }
  start_flag = true;
  for (auto& th : threads)
    th.join();
}

// --- View Un-registration Tests ---
class ResourceRegistryViewUnRegisterTest : public ResourceRegistryTest {
protected:
  TestViewDesc desc1_ { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1 };
  TestViewDesc desc2_ { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2 };
  NativeObject view1_;
  NativeObject view2_;

  void SetUp() override
  {
    ResourceRegistryTest::SetUp();
    view1_ = RegisterView(*resource_, desc1_);
    view2_ = RegisterView(*resource_, desc2_);
  }
};

/*!
 Given two registered views on the same resource, UnRegisterView should remove
 only the specified view and leave the other view and the resource registration
 intact.
*/
NOLINT_TEST_F(ResourceRegistryViewUnRegisterTest, UnregisterSpecificView)
{
  EXPECT_TRUE(registry_->Contains(*resource_, desc1_));
  EXPECT_TRUE(registry_->Contains(*resource_, desc2_));

  registry_->UnRegisterView(*resource_, view1_);

  EXPECT_FALSE(registry_->Contains(*resource_, desc1_));
  EXPECT_TRUE(registry_->Contains(*resource_, desc2_));
  EXPECT_TRUE(registry_->Contains(*resource_));
}

/*!
 UnRegisterViews must remove all views for a resource while keeping the resource
 itself registered in the registry.
*/
NOLINT_TEST_F(ResourceRegistryViewUnRegisterTest, UnregisterAllViews)
{
  EXPECT_TRUE(registry_->Contains(*resource_, desc1_));
  EXPECT_TRUE(registry_->Contains(*resource_, desc2_));

  registry_->UnRegisterViews(*resource_);

  EXPECT_FALSE(registry_->Contains(*resource_, desc1_));
  EXPECT_FALSE(registry_->Contains(*resource_, desc2_));
  EXPECT_TRUE(registry_->Contains(*resource_));
}

/*!
 UnRegisterView with an invalid or non-existent native view must be a safe no-op
 and must not throw exceptions.
*/
NOLINT_TEST_F(ResourceRegistryViewUnRegisterTest, UnregisterNonExistentView)
{
  constexpr NativeObject invalid_view {};
  EXPECT_FALSE(invalid_view.IsValid());
  // Unregistering a non-existent view should not throw
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterView(*resource_, invalid_view));
}

// --- Resource Lifecycle Tests ---
class ResourceRegistryLifecycleTest : public ::testing::Test {
protected:
  std::shared_ptr<MockDescriptorAllocator> allocator_;
  std::unique_ptr<ResourceRegistry> registry_;
  std::shared_ptr<TestResource> resource1_;
  std::shared_ptr<TestResource> resource2_;
  // Capture the segment used for descriptor allocations to verify no leaks.
  FixedDescriptorSegment* last_segment_ { nullptr };

  void SetUp() override
  {
    allocator_
      = std::make_shared<::testing::NiceMock<MockDescriptorAllocator>>();
    allocator_->ext_segment_factory_ = [this](auto capacity, auto base_index,
                                         auto view_type, auto visibility) {
      auto seg = std::make_unique<FixedDescriptorSegment>(
        capacity, base_index, view_type, visibility);
      last_segment_ = seg.get();
      return seg;
    };
    registry_ = std::make_unique<ResourceRegistry>("Test Registry");
    resource1_ = std::make_shared<TestResource>();
    resource2_ = std::make_shared<TestResource>();
    registry_->Register(resource1_);
    registry_->Register(resource2_);
  }
  void TearDown() override
  {
    registry_->UnRegisterResource(*resource1_);
    registry_->UnRegisterResource(*resource2_);
    resource1_.reset();
    resource2_.reset();
    registry_.reset();
    allocator_.reset();
  }

  auto RegisterView(TestResource& resource, TestViewDesc desc) -> NativeObject
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
 Basic lifecycle: with two registered resources, unregister one at a time and
 verify the registry reflects presence/absence accordingly.
*/
NOLINT_TEST_F(
  ResourceRegistryLifecycleTest, RegisterUnregisterMultipleResources)
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
 UnRegisterViews should not remove the resource itself. After calling it, the
 resource must still be reported as present, but view containment must be false
 for the removed view descriptions.
*/
NOLINT_TEST_F(
  ResourceRegistryLifecycleTest, UnregisterViewsDoesNotRemoveResource)
{
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 5 };
  RegisterView(*resource1_, desc);
  EXPECT_TRUE(registry_->Contains(*resource1_));

  registry_->UnRegisterViews(*resource1_);

  EXPECT_TRUE(registry_->Contains(*resource1_));
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
}

// --- Replace behavior tests ---

//! Replace should keep descriptor index stable and recreate view via updater
/*!
 Replace moves descriptor ownership from resource1_ to resource2_ while keeping
 the bindless index stable and recreating the view via the updater. The old
 cache is cleared; a new view is cached for the new resource; and UpdateView
 against the same index must succeed with a new description.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleTest, Replace_RecreateViewAndKeepIndex)
{
  // Arrange
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 42 };
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());
  const auto index = descriptor.GetBindlessHandle();

  const auto old_view
    = registry_->RegisterView(*resource1_, std::move(descriptor), desc);
  ASSERT_TRUE(old_view.IsValid());
  EXPECT_TRUE(registry_->Contains(*resource1_, desc));
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "One descriptor allocated after initial RegisterView";
  }

  // Act: Replace resource1_ with resource2_, recreating the view in-place
  registry_->Replace(*resource1_, resource2_,
    [&](const TestViewDesc& /*old_desc*/) -> std::optional<TestViewDesc> {
      return desc; // recreate the same description
    });

  // Assert: cache moved and updated for new resource
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
  EXPECT_TRUE(registry_->Contains(*resource2_, desc));
  const auto new_view = registry_->Find(*resource2_, desc);
  EXPECT_TRUE(new_view.IsValid());
  EXPECT_NE(new_view, old_view);

  // Index stability: we can still update that exact descriptor index
  constexpr TestViewDesc desc2 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 43 };
  EXPECT_TRUE(registry_->UpdateView(*resource2_, index, desc2));
  EXPECT_TRUE(registry_->Contains(*resource2_, desc2));

  // No leak: still exactly one descriptor allocated; after unregister -> 0
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "Descriptor remains owned post-Replace";
  }
  registry_->UnRegisterResource(*resource2_);
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptor released on UnRegisterResource";
  }
}

//! Replace releases handle when updater returns nullopt
/*!
 When the updater returns nullopt, the descriptor handle is released and not
 transferred. No cached view is present after Replace; subsequent UpdateView
 on the same index must fail because the index is no longer owned.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleTest, Replace_UpdaterNullopt_Releases)
{
  // Arrange
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 77 };
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());
  const auto index = descriptor.GetBindlessHandle();

  const auto old_view
    = registry_->RegisterView(*resource1_, std::move(descriptor), desc);
  ASSERT_TRUE(old_view.IsValid());
  EXPECT_TRUE(registry_->Contains(*resource1_, desc));
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "One descriptor allocated after initial RegisterView";
  }

  // Act: Replace resource1_ with resource2_, updater returns nullopt => release
  registry_->Replace(*resource1_, resource2_,
    [&](const TestViewDesc& /*old_desc*/) -> std::optional<TestViewDesc> {
      return std::nullopt; // release handle
    });

  // Assert: descriptor is no longer on old resource; cache doesn't have view
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc));

  // The descriptor index was freed; UpdateView must fail now
  EXPECT_FALSE(registry_->UpdateView(*resource2_, index, desc));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc));

  // No leak: still exactly one descriptor allocated; after unregister -> 0
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptor released by Replace when updater returns nullopt";
  }
  // Resource2 may remain registered but owns no descriptors
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterResource(*resource2_));
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptor released on UnRegisterResource";
  }
}

//! Replace should work when new resource is not pre-registered (recreate view)
/*!
 Replace when the destination resource is not pre-registered must register it
 internally (without deadlock) and recreate the view in place at the same
 bindless index. The new view is cached; UpdateView at the same index must
 continue to work.
*/
NOLINT_TEST_F(
  ResourceRegistryLifecycleTest, Replace_NewResourceNotRegistered_Recreate)
{
  // Arrange: ensure resource2_ is NOT registered to hit the registration path
  registry_->UnRegisterResource(*resource2_);
  EXPECT_FALSE(registry_->Contains(*resource2_));

  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 314 };
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());
  const auto index = descriptor.GetBindlessHandle();

  const auto old_view
    = registry_->RegisterView(*resource1_, std::move(descriptor), desc);
  ASSERT_TRUE(old_view.IsValid());
  EXPECT_TRUE(registry_->Contains(*resource1_, desc));
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "One descriptor allocated after initial RegisterView";
  }

  // Act: Replace should not deadlock or throw, and must recreate the view
  NOLINT_EXPECT_NO_THROW(registry_->Replace(*resource1_, resource2_,
    [&](const TestViewDesc& /*old_desc*/) -> std::optional<TestViewDesc> {
      return desc; // recreate same description at same index
    }));

  // Assert
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
  EXPECT_TRUE(registry_->Contains(*resource2_, desc));
  const auto new_view = registry_->Find(*resource2_, desc);
  EXPECT_TRUE(new_view.IsValid());
  EXPECT_NE(new_view, old_view);

  // Index stability: can still update that exact descriptor index
  constexpr TestViewDesc desc2 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 315 };
  EXPECT_TRUE(registry_->UpdateView(*resource2_, index, desc2));
  EXPECT_TRUE(registry_->Contains(*resource2_, desc2));

  // No leak: still exactly one descriptor allocated; after unregister -> 0
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "Descriptor remains owned post-Replace (new not pre-registered)";
  }
  registry_->UnRegisterResource(*resource2_);
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptor released on UnRegisterResource";
  }
}

//! Replace with non-registered new resource and updater nullopt releases handle
/*!
 When the destination resource is not pre-registered and the updater returns
 nullopt, Replace must not deadlock and must release the handle (no transfer).
 No cached view exists and UpdateView at the old index must fail.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleTest,
  Replace_NewResourceNotRegistered_UpdaterNullopt_Releases)
{
  // Arrange: ensure resource2_ is NOT registered to hit the registration path
  registry_->UnRegisterResource(*resource2_);
  EXPECT_FALSE(registry_->Contains(*resource2_));

  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2718 };
  DescriptorHandle descriptor
    = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(descriptor.IsValid());
  const auto index = descriptor.GetBindlessHandle();

  const auto old_view
    = registry_->RegisterView(*resource1_, std::move(descriptor), desc);
  ASSERT_TRUE(old_view.IsValid());
  EXPECT_TRUE(registry_->Contains(*resource1_, desc));
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "One descriptor allocated after initial RegisterView";
  }

  // Act: Replace should not deadlock or throw; updater nullopt => release
  NOLINT_EXPECT_NO_THROW(registry_->Replace(*resource1_, resource2_,
    [&](const TestViewDesc& /*old_desc*/) -> std::optional<TestViewDesc> {
      return std::nullopt; // release handle
    }));

  // Assert: descriptor released; no cached view for new resource
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc));

  // Update at the same index must fail since handle was released
  EXPECT_FALSE(registry_->UpdateView(*resource2_, index, desc));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc));

  // No leak: still exactly one descriptor allocated; after unregister -> 0
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptor released by Replace (nullopt, new not pre-registered)";
  }
  registry_->UnRegisterResource(*resource2_);
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptor released on UnRegisterResource";
  }
}

//! Replace with nullptr updater releases all handles (no transfer)
/*!
 When no updater is provided, Replace must release all descriptors owned by the
 old resource and not transfer any to the new one. Indices are freed and any
 UpdateView attempts at those indices must fail.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleTest, Replace_NullUpdater_ReleasesAll)
{
  // Arrange: register two views on resource1_
  constexpr TestViewDesc desc1 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 9001 };
  constexpr TestViewDesc desc2 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 9002 };

  DescriptorHandle h1 = allocator_->Allocate(desc1.view_type, desc1.visibility);
  DescriptorHandle h2 = allocator_->Allocate(desc2.view_type, desc2.visibility);
  ASSERT_TRUE(h1.IsValid());
  ASSERT_TRUE(h2.IsValid());
  const auto i1 = h1.GetBindlessHandle();
  const auto i2 = h2.GetBindlessHandle();
  auto v1 = registry_->RegisterView(*resource1_, std::move(h1), desc1);
  auto v2 = registry_->RegisterView(*resource1_, std::move(h2), desc2);
  ASSERT_TRUE(v1.IsValid());
  ASSERT_TRUE(v2.IsValid());
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 2U)
      << "Two descriptors allocated";
  }

  // Act: use nullptr updater => release all
  registry_->Replace(*resource1_, resource2_, nullptr);

  // Assert: both views gone, indices freed, counts back to 0
  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_FALSE(registry_->Contains(*resource1_, desc2));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc1));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc2));

  EXPECT_FALSE(registry_->UpdateView(*resource2_, i1, desc1));
  EXPECT_FALSE(registry_->UpdateView(*resource2_, i2, desc2));

  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "All descriptors released by null-updater Replace";
  }
}

// --- UpdateView failure semantics tests ---

/*!
 UpdateView must release the descriptor and remove the registration when the
 destination resource produces an invalid view, freeing the index and making
 subsequent updates fail. Mirrors Replace() failure semantics.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleTest, UpdateView_InvalidView_Releases)
{
  // Arrange: register a valid view on resource1_
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 8080 };
  DescriptorHandle h = allocator_->Allocate(desc.view_type, desc.visibility);
  ASSERT_TRUE(h.IsValid());
  const auto idx = h.GetBindlessHandle();
  auto v = registry_->RegisterView(*resource1_, std::move(h), desc);
  ASSERT_TRUE(v.IsValid());
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U);
  }

  // Create a resource that always returns invalid view
  auto bad = std::make_shared<InvalidViewTestResource>();
  registry_->Register(bad);

  // Act: UpdateView to bad resource at the same index should fail and release
  EXPECT_FALSE(registry_->UpdateView(*bad, idx, desc));

  // Assert: index freed, no view cached for bad, no leak
  EXPECT_FALSE(registry_->Contains(*bad, desc));
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
  // The handle was released; allocated count must drop to 0
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U);
  }

  registry_->UnRegisterResource(*bad);
}

// --- Error handling tests for Replace (exceptions are swallowed, handles safe)
// ---

/*!
 Updater throws for one view and succeeds for another: the failing view's
 descriptor must be released (no transfer); the other must be recreated in
 place and keep its index. Replace must not throw.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleTest,
  Replace_UpdaterThrows_ReleasesOne_RecreatesOther)
{
  // Arrange: two views on resource1_
  constexpr TestViewDesc d1 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 11 };
  constexpr TestViewDesc d2 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 12 };

  DescriptorHandle h1 = allocator_->Allocate(d1.view_type, d1.visibility);
  DescriptorHandle h2 = allocator_->Allocate(d2.view_type, d2.visibility);
  ASSERT_TRUE(h1.IsValid());
  ASSERT_TRUE(h2.IsValid());
  const auto i1 = h1.GetBindlessHandle();
  const auto i2 = h2.GetBindlessHandle();
  auto v1 = registry_->RegisterView(*resource1_, std::move(h1), d1);
  auto v2 = registry_->RegisterView(*resource1_, std::move(h2), d2);
  ASSERT_TRUE(v1.IsValid());
  ASSERT_TRUE(v2.IsValid());

  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 2U)
      << "Two descriptors allocated";
  }

  // Act: updater throws for d1, succeeds for d2
  NOLINT_EXPECT_NO_THROW(registry_->Replace(*resource1_, resource2_,
    [&](const TestViewDesc& prev) -> std::optional<TestViewDesc> {
      if (prev.id == d1.id) {
        throw std::runtime_error("updater failure");
      }
      return prev; // recreate same desc
    }));

  // Assert: d1 was released (no owner), d2 transferred to resource2_
  EXPECT_FALSE(registry_->Contains(*resource1_, d1));
  EXPECT_FALSE(registry_->Contains(*resource2_, d1));
  EXPECT_FALSE(registry_->UpdateView(*resource2_, i1, d1));

  EXPECT_FALSE(registry_->Contains(*resource1_, d2));
  EXPECT_TRUE(registry_->Contains(*resource2_, d2));
  constexpr TestViewDesc d2b { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 13 };
  EXPECT_TRUE(registry_->UpdateView(*resource2_, i2, d2b));
  EXPECT_TRUE(registry_->Contains(*resource2_, d2b));

  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "One descriptor remains after partial transfer";
  }
  registry_->UnRegisterResource(*resource2_);
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptors released on cleanup";
  }
}

// Fixture using ThrowingTestResource to simulate GetNativeView exceptions
class ResourceRegistryLifecycleThrowingTest : public ::testing::Test {
protected:
  std::shared_ptr<MockDescriptorAllocator> allocator_;
  std::unique_ptr<ResourceRegistry> registry_;
  std::shared_ptr<ThrowingTestResource> resource1_;
  std::shared_ptr<ThrowingTestResource> resource2_;
  FixedDescriptorSegment* last_segment_ { nullptr };

  void SetUp() override
  {
    allocator_
      = std::make_shared<::testing::NiceMock<MockDescriptorAllocator>>();
    allocator_->ext_segment_factory_ = [this](auto capacity, auto base_index,
                                         auto view_type, auto visibility) {
      auto seg = std::make_unique<FixedDescriptorSegment>(
        capacity, base_index, view_type, visibility);
      last_segment_ = seg.get();
      return seg;
    };
    registry_ = std::make_unique<ResourceRegistry>("Test Registry");
    resource1_ = std::make_shared<ThrowingTestResource>();
    resource2_ = std::make_shared<ThrowingTestResource>();
    registry_->Register(resource1_);
    registry_->Register(resource2_);
  }

  void TearDown() override
  {
    registry_->UnRegisterResource(*resource1_);
    registry_->UnRegisterResource(*resource2_);
    resource1_.reset();
    resource2_.reset();
    registry_.reset();
    allocator_.reset();
  }
};

/*!
 GetNativeView throws for one view on the destination resource: that descriptor
 must be released (no transfer), while other views are recreated in place and
 keep their indices. Replace must not throw.
*/
NOLINT_TEST_F(ResourceRegistryLifecycleThrowingTest,
  Replace_GetNativeViewThrows_ReleasesOne_RecreatesOther)
{
  // Arrange: two views on resource1_
  constexpr TestViewDesc d1 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 21 };
  constexpr TestViewDesc d2 { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 22 };
  // Make resource2_ throw on d1 during recreate
  resource2_->SetThrowOnId(d1.id);

  DescriptorHandle h1 = allocator_->Allocate(d1.view_type, d1.visibility);
  DescriptorHandle h2 = allocator_->Allocate(d2.view_type, d2.visibility);
  ASSERT_TRUE(h1.IsValid());
  ASSERT_TRUE(h2.IsValid());
  const auto i1 = h1.GetBindlessHandle();
  const auto i2 = h2.GetBindlessHandle();
  auto v1 = registry_->RegisterView(*resource1_, std::move(h1), d1);
  auto v2 = registry_->RegisterView(*resource1_, std::move(h2), d2);
  ASSERT_TRUE(v1.IsValid());
  ASSERT_TRUE(v2.IsValid());

  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 2U)
      << "Two descriptors allocated";
  }

  // Act: updater requests recreate for both; GetNativeView will throw for d1
  NOLINT_EXPECT_NO_THROW(registry_->Replace(*resource1_, resource2_,
    [&](const TestViewDesc& prev) -> std::optional<TestViewDesc> {
      return prev;
    }));

  // Assert: d1 released, d2 transferred
  EXPECT_FALSE(registry_->Contains(*resource1_, d1));
  EXPECT_FALSE(registry_->Contains(*resource2_, d1));
  EXPECT_FALSE(registry_->UpdateView(*resource2_, i1, d1));

  EXPECT_FALSE(registry_->Contains(*resource1_, d2));
  EXPECT_TRUE(registry_->Contains(*resource2_, d2));
  constexpr TestViewDesc d2b { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 23 };
  EXPECT_TRUE(registry_->UpdateView(*resource2_, i2, d2b));
  EXPECT_TRUE(registry_->Contains(*resource2_, d2b));

  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 1U)
      << "One descriptor remains after partial transfer";
  }
  registry_->UnRegisterResource(*resource2_);
  if (last_segment_ && last_segment_->GetCapacity().get() > 0) {
    EXPECT_EQ(last_segment_->GetAllocatedCount().get(), 0U)
      << "Descriptors released on cleanup";
  }
}

} // namespace
