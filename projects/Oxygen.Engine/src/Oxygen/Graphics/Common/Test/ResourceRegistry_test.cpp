//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorAllocator.h>

using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeObject;
using oxygen::graphics::RegisteredResource;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::FixedDescriptorHeapSegment;

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
        return id == other.id && view_type == other.view_type && visibility == other.visibility;
    }
};

class TestResource final : public RegisteredResource, public oxygen::Object {
    OXYGEN_TYPED(TestResource)
public:
    using ViewDescriptionT = TestViewDesc;

    // Required by resource registry
    [[maybe_unused]] auto GetNativeView(const ViewDescriptionT& desc) -> NativeObject
    {
        // Use the resource pointer and view id to make each view unique per resource and description
        const uint64_t ptr = reinterpret_cast<uint64_t>(this);
        const uint64_t id = static_cast<uint64_t>(desc.id);
        return { (ptr << 16) | (id & 0xFFFF), ClassTypeId() };
    }
};
} // namespace

template <>
struct std::hash<TestViewDesc> {
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
        allocator_ = std::make_shared<::testing::NiceMock<MockDescriptorAllocator>>();
        allocator_->ext_segment_factory_ =
            [](auto capacity, auto base_index, auto view_type, auto visibility) {
                return std::make_unique<FixedDescriptorHeapSegment>(
                    capacity, base_index,
                    view_type, visibility);
            };

        registry_ = std::make_unique<ResourceRegistry>(allocator_);
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
};

// --- Resource Registration Tests ---
class ResourceRegistryRegistrationTest : public ResourceRegistryTest { };

NOLINT_TEST_F(ResourceRegistryRegistrationTest, RegisterAndContains)
{
    EXPECT_TRUE(registry_->Contains(*resource_));
}

NOLINT_TEST_F(ResourceRegistryRegistrationTest, DoubleRegisterAndUnregister)
{
    registry_->Register(resource_);
    EXPECT_TRUE(registry_->Contains(*resource_));

    registry_->UnRegisterResource(*resource_);
    EXPECT_FALSE(registry_->Contains(*resource_));

    registry_->UnRegisterResource(*resource_);
    EXPECT_FALSE(registry_->Contains(*resource_));
}

// --- View Caching and Uniqueness Tests ---
class ResourceRegistryViewCacheTest : public ResourceRegistryTest { };

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewAlreadyRegistered)
{
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 42
    };
    const auto view1 = registry_->RegisterView(*resource_, desc);
    EXPECT_TRUE(view1.IsValid());
    EXPECT_TRUE(registry_->Contains(*resource_, desc));

    // Registering the same view again should throw a runtime error
    EXPECT_THROW(
        registry_->RegisterView(*resource_, desc),
        std::runtime_error)
        << "Registering the same view again should throw a runtime error";
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewDifferentDescriptions)
{
    constexpr TestViewDesc desc1 {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 1
    };
    constexpr TestViewDesc desc2 {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 2
    };
    const auto view1 = registry_->RegisterView(*resource_, desc1);
    const auto view2 = registry_->RegisterView(*resource_, desc2);
    EXPECT_TRUE(view1.IsValid());
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Different descriptions should yield different views";
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewCacheEviction)
{
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 77
    };
    const auto view1 = registry_->RegisterView(*resource_, desc);
    registry_->UnRegisterViews(*resource_);
    // Allocate a new resource to guarantee a new pointer
    const auto resource2 = std::make_shared<TestResource>();
    registry_->Register(resource2);
    const auto view2 = registry_->RegisterView(*resource2, desc);
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Cache should be cleared after UnRegisterViews, "
                               "new view should be created for new resource instance";

    // Cleanup
    registry_->UnRegisterResource(*resource2);
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewMultipleResources)
{
    const auto resource2 = std::make_shared<TestResource>();
    registry_->Register(resource2);
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 123
    };
    const auto view1 = registry_->RegisterView(*resource_, desc);
    const auto view2 = registry_->RegisterView(*resource2, desc);
    EXPECT_TRUE(view1.IsValid());
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Same description on different resources should yield different views";

    // Cleanup
    registry_->UnRegisterResource(*resource2);
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewAfterUnregisterResource)
{
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 55
    };
    const auto view1 = registry_->RegisterView(*resource_, desc);
    registry_->UnRegisterResource(*resource_);
    // Allocate a new resource to guarantee a new pointer
    resource_ = std::make_shared<TestResource>();
    registry_->Register(resource_);
    const auto view2 = registry_->RegisterView(*resource_, desc);
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Re-registering with a new resource instance should not return stale view";
}

// --- Error Handling Tests ---
class ResourceRegistryErrorTest : public ResourceRegistryTest { };

NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterViewForUnregisteredResource)
{
    const auto unregistered_resource = std::make_shared<TestResource>();
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 99
    };
    const auto view_object = registry_->RegisterView(*unregistered_resource, desc);
    EXPECT_FALSE(view_object.IsValid());
}

NOLINT_TEST_F(ResourceRegistryErrorTest, FindViewForUnregisteredResource)
{
    const auto unregistered_resource = std::make_shared<TestResource>();
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 100
    };
    EXPECT_FALSE(registry_->Contains(*unregistered_resource, desc));
    EXPECT_FALSE(registry_->Find(*unregistered_resource, desc).IsValid());
}

NOLINT_TEST_F(ResourceRegistryErrorTest, DescriptorAllocatorFailure)
{
    EXPECT_CALL(*allocator_, Allocate(::testing::_, ::testing::_))
        .WillOnce(::testing::Return(oxygen::graphics::DescriptorHandle {}));

    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 101
    };

    const auto view_object = registry_->RegisterView(*resource_, desc);
    EXPECT_FALSE(view_object.IsValid());
}

NOLINT_TEST_F(ResourceRegistryErrorTest, UnRegisterNonExistentResource)
{
    const auto non_existent_resource = std::make_shared<TestResource>();
    NOLINT_EXPECT_NO_THROW(registry_->UnRegisterResource(*non_existent_resource));
    EXPECT_FALSE(registry_->Contains(*non_existent_resource));
}

// --- Concurrency Tests ---
class ResourceRegistryConcurrencyTest : public ResourceRegistryTest { };

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
                TestViewDesc desc {
                    .view_type = ResourceViewType::kConstantBuffer,
                    .visibility = DescriptorVisibility::kShaderVisible,
                    .id = i
                };
                auto view = registry_->RegisterView(*resources[t], desc);
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
    TestViewDesc desc1_ {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 1
    };
    TestViewDesc desc2_ {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 2
    };
    NativeObject view1_;
    NativeObject view2_;

    void SetUp() override
    {
        ResourceRegistryTest::SetUp();
        view1_ = registry_->RegisterView(*resource_, desc1_);
        view2_ = registry_->RegisterView(*resource_, desc2_);
    }
};

NOLINT_TEST_F(ResourceRegistryViewUnRegisterTest, UnregisterSpecificView)
{
    EXPECT_TRUE(registry_->Contains(*resource_, desc1_));
    EXPECT_TRUE(registry_->Contains(*resource_, desc2_));

    registry_->UnRegisterView(*resource_, view1_);

    EXPECT_FALSE(registry_->Contains(*resource_, desc1_));
    EXPECT_TRUE(registry_->Contains(*resource_, desc2_));
    EXPECT_TRUE(registry_->Contains(*resource_));
}

NOLINT_TEST_F(ResourceRegistryViewUnRegisterTest, UnregisterAllViews)
{
    EXPECT_TRUE(registry_->Contains(*resource_, desc1_));
    EXPECT_TRUE(registry_->Contains(*resource_, desc2_));

    registry_->UnRegisterViews(*resource_);

    EXPECT_FALSE(registry_->Contains(*resource_, desc1_));
    EXPECT_FALSE(registry_->Contains(*resource_, desc2_));
    EXPECT_TRUE(registry_->Contains(*resource_));
}

NOLINT_TEST_F(ResourceRegistryViewUnRegisterTest, UnregisterNonExistentView)
{
    constexpr NativeObject invalid_view {};
    EXPECT_FALSE(invalid_view.IsValid());
    NOLINT_EXPECT_NO_THROW(registry_->UnRegisterView(*resource_, invalid_view));
}

// --- Resource Lifecycle Tests ---
class ResourceRegistryLifecycleTest : public ::testing::Test {
protected:
    std::shared_ptr<MockDescriptorAllocator> allocator_;
    std::unique_ptr<ResourceRegistry> registry_;
    std::shared_ptr<TestResource> resource1_;
    std::shared_ptr<TestResource> resource2_;

    void SetUp() override
    {
        allocator_ = std::make_shared<::testing::NiceMock<MockDescriptorAllocator>>();
        allocator_->ext_segment_factory_ =
            [](auto capacity, auto base_index, auto view_type, auto visibility) {
                return std::make_unique<FixedDescriptorHeapSegment>(
                    capacity, base_index,
                    view_type, visibility);
            };
        registry_ = std::make_unique<ResourceRegistry>(allocator_);
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
};

NOLINT_TEST_F(ResourceRegistryLifecycleTest, RegisterUnregisterMultipleResources)
{
    EXPECT_TRUE(registry_->Contains(*resource1_));
    EXPECT_TRUE(registry_->Contains(*resource2_));
    registry_->UnRegisterResource(*resource1_);
    EXPECT_FALSE(registry_->Contains(*resource1_));
    EXPECT_TRUE(registry_->Contains(*resource2_));
    registry_->UnRegisterResource(*resource2_);
    EXPECT_FALSE(registry_->Contains(*resource2_));
}

NOLINT_TEST_F(ResourceRegistryLifecycleTest, UnregisterViewsDoesNotRemoveResource)
{
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 5
    };
    registry_->RegisterView(*resource1_, desc);
    EXPECT_TRUE(registry_->Contains(*resource1_));

    registry_->UnRegisterViews(*resource1_);

    EXPECT_TRUE(registry_->Contains(*resource1_));
    EXPECT_FALSE(registry_->Contains(*resource1_, desc));
}

} // namespace
