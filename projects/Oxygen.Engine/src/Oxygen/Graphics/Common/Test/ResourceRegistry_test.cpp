//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>
#include <thread>
#include <vector>
#include <atomic>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>

using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeObject;
using oxygen::graphics::RegisteredResource;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using oxygen::graphics::detail::DescriptorHeapSegment;
using oxygen::graphics::detail::FixedDescriptorHeapSegment;

using oxygen::graphics::bindless::testing::MockDescriptorAllocator;

namespace {

// Minimal test resource and view description
struct TestViewDesc {
    ResourceViewType view_type { ResourceViewType::kConstantBuffer };
    DescriptorVisibility visibility { DescriptorVisibility::kShaderVisible };
    int id { 0 };

    auto operator==(const TestViewDesc& other) const -> bool
    {
        return id == other.id && view_type == other.view_type && visibility == other.visibility;
    }
};

class TestResource final : public RegisteredResource, public oxygen::Object {
    OXYGEN_TYPED(TestResource)
public:
    using ViewDescriptionT = TestViewDesc;
    NativeObject GetNativeView(const ViewDescriptionT& desc)
    {
        // Use the resource pointer and view id to make each view unique per resource and description
        uint64_t ptr = reinterpret_cast<uint64_t>(this);
        uint64_t id = static_cast<uint64_t>(desc.id);
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

    static constexpr DescriptorHeapSegment::IndexT HEAP_CAPACITY = 10;
    static constexpr DescriptorHeapSegment::IndexT HEAP_BASE_INDEX = 0;

    void SetUp() override
    {
        allocator_ = std::make_shared<::testing::NiceMock<MockDescriptorAllocator>>(BaseDescriptorAllocatorConfig {});
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
};

// --- Resource Registration Tests ---
class ResourceRegistryRegistrationTest : public ResourceRegistryTest {};

NOLINT_TEST_F(ResourceRegistryRegistrationTest, RegisterAndContains)
{
    EXPECT_TRUE(registry_->Contains(*resource_));
}

NOLINT_TEST_F(ResourceRegistryRegistrationTest, DoubleRegisterAndUnregister)
{
    registry_->Register(resource_);
    EXPECT_TRUE(registry_->Contains(*resource_));

    registry_->UnRegister(*resource_);
    EXPECT_FALSE(registry_->Contains(*resource_));

    registry_->UnRegister(*resource_);
    EXPECT_FALSE(registry_->Contains(*resource_));
}

// --- View Caching and Uniqueness Tests ---
class ResourceRegistryViewCacheTest : public ResourceRegistryTest {};

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewCachingAndReuse)
{
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 42
    };
    auto view1 = registry_->RegisterView(*resource_, desc);
    auto view2 = registry_->RegisterView(*resource_, desc);
    EXPECT_TRUE(view1.IsValid());
    EXPECT_EQ(view1, view2) << "View should be cached and reused for same description";
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
    auto view1 = registry_->RegisterView(*resource_, desc1);
    auto view2 = registry_->RegisterView(*resource_, desc2);
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
    auto view1 = registry_->RegisterView(*resource_, desc);
    registry_->UnRegisterViews(NativeObject(resource_.get(), resource_->ClassTypeId()));
    // Allocate a new resource to guarantee a new pointer
    auto resource2 = std::make_shared<TestResource>();
    registry_->Register(resource2);
    auto view2 = registry_->RegisterView(*resource2, desc);
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Cache should be cleared after UnRegisterViews, new view should be created for new resource instance";
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewMultipleResources)
{
    auto resource2 = std::make_shared<TestResource>();
    registry_->Register(resource2);
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 123
    };
    auto view1 = registry_->RegisterView(*resource_, desc);
    auto view2 = registry_->RegisterView(*resource2, desc);
    EXPECT_TRUE(view1.IsValid());
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Same description on different resources should yield different views";
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewAfterUnregisterResource)
{
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 55
    };
    auto view1 = registry_->RegisterView(*resource_, desc);
    registry_->UnRegister(*resource_);
    // Allocate a new resource to guarantee a new pointer
    resource_ = std::make_shared<TestResource>();
    registry_->Register(resource_);
    auto view2 = registry_->RegisterView(*resource_, desc);
    EXPECT_TRUE(view2.IsValid());
    EXPECT_NE(view1, view2) << "Re-registering with a new resource instance should not return stale view";
}

NOLINT_TEST_F(ResourceRegistryViewCacheTest, RegisterViewInvalidResource)
{
    TestResource* null_resource = nullptr;
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 99
    };
    auto view_object = registry_->RegisterView(*null_resource, desc);
    EXPECT_FALSE(view_object.IsValid());
}

// --- Error Handling Tests ---
class ResourceRegistryErrorTest : public ResourceRegistryTest {};

NOLINT_TEST_F(ResourceRegistryErrorTest, RegisterViewForUnregisteredResource)
{
    auto unregistered_resource = std::make_shared<TestResource>();
    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 99
    };
    auto view_object = registry_->RegisterView(*unregistered_resource, desc);
    EXPECT_FALSE(view_object.IsValid());
}

NOLINT_TEST_F(ResourceRegistryErrorTest, FindViewForUnregisteredResource)
{
    auto unregistered_resource = std::make_shared<TestResource>();
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
        .WillOnce(::testing::Return(oxygen::graphics::DescriptorHandle{}));

    constexpr TestViewDesc desc {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = DescriptorVisibility::kShaderVisible,
        .id = 101
    };

    auto view_object = registry_->RegisterView(*resource_, desc);
    EXPECT_FALSE(view_object.IsValid());
}

NOLINT_TEST_F(ResourceRegistryErrorTest, UnRegisterNonExistentResource)
{
    auto non_existent_resource = std::make_shared<TestResource>();
    NOLINT_EXPECT_NO_THROW(registry_->UnRegister(*non_existent_resource));
    EXPECT_FALSE(registry_->Contains(*non_existent_resource));
}

NOLINT_TEST_F(ResourceRegistryErrorTest, UnRegisterViewsForNonExistentResource)
{
    NativeObject non_existent_native_object {
        reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEF)),
        TestResource::ClassTypeId()
    };
    NOLINT_EXPECT_NO_THROW(registry_->UnRegisterViews(non_existent_native_object));
}

// --- Concurrency Tests ---
class ResourceRegistryConcurrencyTest : public ResourceRegistryTest {};

NOLINT_TEST_F(ResourceRegistryConcurrencyTest, ConcurrentRegisterAndUnregister)
{
    constexpr int kNumThreads = 8;
    constexpr int kNumIterations = 100;
    std::atomic<bool> start_flag{false};
    std::vector<std::thread> threads;
    std::vector<std::shared_ptr<TestResource>> resources(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i) {
        resources[i] = std::make_shared<TestResource>();
    }
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&, t]() {
            while (!start_flag.load()) { std::this_thread::yield(); }
            for (int i = 0; i < kNumIterations; ++i) {
                registry_->Register(resources[t]);
                TestViewDesc desc{ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible, i};
                auto view = registry_->RegisterView(*resources[t], desc);
                EXPECT_TRUE(view.IsValid());
                registry_->UnRegister(*resources[t]);
            }
        });
    }
    start_flag = true;
    for (auto& th : threads) th.join();
}

} // namespace
