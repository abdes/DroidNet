//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceViewType;

using enum ResourceViewType;
using enum DescriptorVisibility;

namespace {

// Mock implementation of DescriptorAllocator for testing using Google Mock
// ReSharper disable once CppClassCanBeFinal -- Mock classes should not be final
class MockDescriptorAllocator : public DescriptorAllocator {
public:
    // Define mock methods using Google Mock macros
    // NOLINTBEGIN
    MOCK_METHOD(DescriptorHandle, Allocate, (ResourceViewType view_type, DescriptorVisibility visibility), (override));
    MOCK_METHOD(void, Release, (DescriptorHandle & handle), (override));
    MOCK_METHOD(void, CopyDescriptor, (const DescriptorHandle& source, const DescriptorHandle& destination), (override));
    MOCK_METHOD(NativeObject, GetNativeHandle, (const DescriptorHandle& handle), (const, override));
    MOCK_METHOD(void, PrepareForRendering, (const NativeObject& command_list), (override));
    MOCK_METHOD(uint32_t, GetRemainingDescriptors, (ResourceViewType view_type, DescriptorVisibility visibility), (const, override));
    // NOLINTEND

    MockDescriptorAllocator()
    {
        uint32_t next_index = 0;

        // Default Allocate implementation
        ON_CALL(*this, Allocate(::testing::_, ::testing::_))
            .WillByDefault(
                [this, next_index](const ResourceViewType view_type, const DescriptorVisibility visibility) mutable {
                    const uint32_t index = next_index++;
                    return DescriptorHandle(this, index, view_type, visibility);
                });

        // Default Release implementation (no-op)
        ON_CALL(*this, Release(::testing::_))
            .WillByDefault([](DescriptorHandle&) { });

        // Default GetNativeHandle implementation
        ON_CALL(*this, GetNativeHandle(::testing::_))
            .WillByDefault(
                [](const DescriptorHandle&) {
                    static constexpr oxygen::TypeId dummy_type_id = 1;
                    return NativeObject(nullptr, dummy_type_id);
                });

        // Default GetRemainingDescriptors implementation
        ON_CALL(*this, GetRemainingDescriptors(::testing::_, ::testing::_))
            .WillByDefault(testing::Return(1000));
    }
};

// Test scenarios
NOLINT_TEST(DescriptorHandle, DefaultConstructionIsInvalid)
{
    const DescriptorHandle handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), ~0u);
}

NOLINT_TEST(DescriptorHandle, AllocatedHandleIsValid)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(kTexture_SRV, kShaderVisible)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(1);

    const auto handle = allocator.Allocate(kTexture_SRV, kShaderVisible);

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), 0u);
    EXPECT_EQ(handle.GetViewType(), kTexture_SRV);
    EXPECT_EQ(handle.GetVisibility(), kShaderVisible);
}

NOLINT_TEST(DescriptorHandle, MoveConstructorTransfersOwnership)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(1);

    auto handle1 = allocator.Allocate(kTexture_SRV, kShaderVisible);

    const DescriptorHandle handle2(std::move(handle1));

    EXPECT_FALSE(handle1.IsValid()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(handle1.GetIndex(), ~0u);

    EXPECT_TRUE(handle2.IsValid());
    EXPECT_EQ(handle2.GetIndex(), 0u);
    EXPECT_EQ(handle2.GetViewType(), kTexture_SRV);
}

NOLINT_TEST(DescriptorHandle, MoveAssignmentTransfersOwnership)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(::testing::_, ::testing::_)).Times(2);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(2);

    auto handle1 = allocator.Allocate(kTexture_SRV, kShaderVisible);
    auto handle2 = allocator.Allocate(kConstantBuffer, kCpuOnly);

    EXPECT_TRUE(handle1.IsValid());
    EXPECT_TRUE(handle2.IsValid());
    EXPECT_EQ(handle1.GetIndex(), 0u);
    EXPECT_EQ(handle2.GetIndex(), 1u);
    EXPECT_EQ(handle1.GetViewType(), kTexture_SRV);
    EXPECT_EQ(handle2.GetViewType(), kConstantBuffer);

    handle2 = std::move(handle1);

    EXPECT_FALSE(handle1.IsValid()); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(handle1.GetIndex(), ~0u);

    EXPECT_TRUE(handle2.IsValid());
    EXPECT_EQ(handle2.GetIndex(), 0u);
    EXPECT_EQ(handle2.GetViewType(), kTexture_SRV);
}

NOLINT_TEST(DescriptorHandle, ExplicitReleaseInvalidatesHandle)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(1);

    auto handle = allocator.Allocate(kTexture_SRV, kShaderVisible);

    EXPECT_TRUE(handle.IsValid());

    handle.Release();

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), ~0u);
}

NOLINT_TEST(DescriptorHandle, DestructorReleasesHandle)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(1);

    {
        const auto handle = allocator.Allocate(kTexture_SRV, kShaderVisible);
        EXPECT_TRUE(handle.IsValid());
        EXPECT_EQ(handle.GetIndex(), 0u);
    }
}

NOLINT_TEST(DescriptorHandle, MultipleSpacesAllocateCorrectly)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(kTexture_SRV, kShaderVisible)).Times(1);
    EXPECT_CALL(allocator, Allocate(kTexture_SRV, kCpuOnly)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(2);

    const auto shader_visible_handle = allocator.Allocate(kTexture_SRV, kShaderVisible);
    const auto non_shader_visible_handle = allocator.Allocate(kTexture_SRV, kCpuOnly);

    EXPECT_EQ(shader_visible_handle.GetVisibility(), kShaderVisible);
    EXPECT_EQ(non_shader_visible_handle.GetVisibility(), kCpuOnly);

    EXPECT_EQ(shader_visible_handle.GetViewType(), kTexture_SRV);
    EXPECT_EQ(non_shader_visible_handle.GetViewType(), kTexture_SRV);
}

NOLINT_TEST(DescriptorHandle, ReleasingAnInvalidHandleIsNoop)
{
    MockDescriptorAllocator allocator;
    EXPECT_CALL(allocator, Release(::testing::_)).Times(0);

    DescriptorHandle handle;
    handle.Release();

    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST(DescriptorHandle, DifferentResourceViewTypesAllocateCorrectly)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(kTexture_SRV, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Allocate(kTexture_UAV, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Allocate(kConstantBuffer, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Allocate(kSampler, ::testing::_)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(4);

    const auto srv_handle = allocator.Allocate(kTexture_SRV, kShaderVisible);
    const auto uav_handle = allocator.Allocate(kTexture_UAV, kShaderVisible);
    const auto cbv_handle = allocator.Allocate(kConstantBuffer, kShaderVisible);
    const auto sampler_handle = allocator.Allocate(kSampler, kShaderVisible);

    EXPECT_EQ(srv_handle.GetViewType(), kTexture_SRV);
    EXPECT_EQ(uav_handle.GetViewType(), kTexture_UAV);
    EXPECT_EQ(cbv_handle.GetViewType(), kConstantBuffer);
    EXPECT_EQ(sampler_handle.GetViewType(), kSampler);

    EXPECT_EQ(srv_handle.GetVisibility(), kShaderVisible);
    EXPECT_EQ(uav_handle.GetVisibility(), kShaderVisible);
    EXPECT_EQ(cbv_handle.GetVisibility(), kShaderVisible);
    EXPECT_EQ(sampler_handle.GetVisibility(), kShaderVisible);
}

NOLINT_TEST(DescriptorHandle, CopyBetweenSpaces)
{
    testing::StrictMock<MockDescriptorAllocator> allocator;

    const DescriptorHandle src_handle(nullptr, 5, kTexture_SRV, kCpuOnly);
    const DescriptorHandle dst_handle(nullptr, 10, kTexture_SRV, kShaderVisible);

    EXPECT_CALL(allocator, CopyDescriptor(::testing::_, ::testing::_))
        .WillOnce(testing::DoAll(
            [&](const DescriptorHandle& source, const DescriptorHandle& dest) {
                EXPECT_EQ(source.GetIndex(), 5u);
                EXPECT_EQ(source.GetViewType(), kTexture_SRV);
                EXPECT_EQ(source.GetVisibility(), kCpuOnly);

                EXPECT_EQ(dest.GetIndex(), 10u);
                EXPECT_EQ(dest.GetViewType(), kTexture_SRV);
                EXPECT_EQ(dest.GetVisibility(), kShaderVisible);
            }));

    allocator.CopyDescriptor(src_handle, dst_handle);
}

NOLINT_TEST(DescriptorHandle, GetRemainingDescriptors)
{
    const MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, GetRemainingDescriptors(kTexture_SRV, kShaderVisible))
        .WillOnce(testing::Return(100));
    EXPECT_CALL(allocator, GetRemainingDescriptors(kTexture_SRV, kCpuOnly))
        .WillOnce(testing::Return(200));
    EXPECT_CALL(allocator, GetRemainingDescriptors(kSampler, kShaderVisible))
        .WillOnce(testing::Return(50));

    EXPECT_EQ(100u, allocator.GetRemainingDescriptors(kTexture_SRV, kShaderVisible));
    EXPECT_EQ(200u, allocator.GetRemainingDescriptors(kTexture_SRV, kCpuOnly));
    EXPECT_EQ(50u, allocator.GetRemainingDescriptors(kSampler, kShaderVisible));
}

NOLINT_TEST(DescriptorHandle, GetNativeHandle)
{
    MockDescriptorAllocator allocator;

    EXPECT_CALL(allocator, Allocate(kTexture_SRV, kShaderVisible)).Times(1);
    EXPECT_CALL(allocator, Release(::testing::_)).Times(1);

    const auto handle = allocator.Allocate(kTexture_SRV, kShaderVisible);

    static constexpr oxygen::TypeId test_type_id = 42;
    const auto test_pointer = reinterpret_cast<void*>(0x12345678);
    const NativeObject mock_native(test_pointer, test_type_id);

    EXPECT_CALL(allocator, GetNativeHandle(::testing::_))
        .WillOnce(testing::Return(mock_native));

    const auto native_handle = allocator.GetNativeHandle(handle);

    EXPECT_EQ(native_handle.OwnerTypeId(), test_type_id);
    EXPECT_EQ(native_handle.AsPointer<void>(), test_pointer);
}

} // anonymous namespace
