//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/ResourceHandle.h>

#include <Oxygen/Testing/GTest.h>

using oxygen::ResourceHandle;

namespace {

NOLINT_TEST(ResourceHandleTest, InvalidHandle)
{
    const ResourceHandle handle;
    ASSERT_FALSE(handle.IsValid());
}

NOLINT_TEST(ResourceHandleTest, ToString)
{
    const ResourceHandle handle(1U, 0x04);
    const std::string expected_string = "ResourceHandle(Index: 1, ResourceType: 4, Generation: 0, IsFree: false)";
    EXPECT_EQ(handle.ToString(), expected_string);
}

NOLINT_TEST(ResourceHandleTest, ToString_InvalidHandle)
{
    const ResourceHandle handle;
    ASSERT_FALSE(handle.IsValid());
    const std::string expected_string = "ResourceHandle(Invalid)";
    EXPECT_EQ(handle.ToString(), expected_string);
}

NOLINT_TEST(ResourceHandleTest, ValidHandle)
{
    const ResourceHandle handle(1U, 0x04);
    EXPECT_EQ(handle.Index(), 1U);
    EXPECT_EQ(handle.ResourceType(), 0x04);
    EXPECT_EQ(handle.Generation(), 0);
}

NOLINT_TEST(ResourceHandleTest, Comparison)
{
    // Arrange & Act
    const ResourceHandle handle1(1U, 0x04);
    const ResourceHandle handle2(1U, 0x04);
    const ResourceHandle handle3(2U, 0x04);

    // Assert
    EXPECT_TRUE(handle1 == handle2);
    EXPECT_TRUE(handle1 < handle3);
    EXPECT_TRUE(handle1 != handle3);
}

NOLINT_TEST(ResourceHandleTest, GetHandle)
{
    const ResourceHandle handle(1U, 0x04);
    const ResourceHandle::HandleT the_handle = handle.Handle();
    EXPECT_EQ(the_handle, 0x0004'0000'0000'0001ULL);
}

NOLINT_TEST(ResourceHandleTest, NewGeneration)
{
    ResourceHandle handle(1U, 0x03);
    ASSERT_EQ(handle.Generation(), 0);
    for (ResourceHandle::GenerationT gen = 0;
        gen < ResourceHandle::kGenerationMax;
        gen++) {
        handle.NewGeneration();
        ASSERT_EQ(handle.Index(), 1U);
        ASSERT_EQ(handle.ResourceType(), 0x03);
        ASSERT_EQ(handle.Generation(), gen + 1);
    }
#if defined(ASAP_IS_DEBUG_BUILD)
    ASSERT_DEATH(handle.NewGeneration(), "");
#else
    handle.NewGeneration();
    ASSERT_EQ(handle.Generation(), 0);
#endif
}

NOLINT_TEST(ResourceHandleTest, SetResourceType)
{
    ResourceHandle handle(1U);
    EXPECT_EQ(handle.ResourceType(), ResourceHandle::kTypeNotInitialized);
    handle.SetResourceType(0x12);
    EXPECT_EQ(handle.ResourceType(), 0x12);
}

NOLINT_TEST(ResourceHandleTest, SetIndex)
{
    ResourceHandle handle;
    handle.SetIndex(0);
    EXPECT_EQ(handle.Index(), 0U);
    constexpr ResourceHandle::IndexT kValidIndex = 12345;
    handle.SetIndex(kValidIndex);
    EXPECT_EQ(handle.Index(), kValidIndex);
}

NOLINT_TEST(ResourceHandleTest, SetFree)
{
    ResourceHandle handle(1U, 0x03);
    handle.NewGeneration();
    EXPECT_EQ(handle.Index(), 1U);
    EXPECT_EQ(handle.ResourceType(), 0x03);
    EXPECT_EQ(handle.Generation(), 1);
    ASSERT_FALSE(handle.IsFree());
    handle.SetFree(true);
    EXPECT_TRUE(handle.IsFree());
    EXPECT_EQ(handle.Index(), 1U);
    EXPECT_EQ(handle.ResourceType(), 0x03);
    ASSERT_EQ(handle.Generation(), 1);
    handle.SetFree(false);
    ASSERT_FALSE(handle.IsFree());
    EXPECT_EQ(handle.Index(), 1U);
    EXPECT_EQ(handle.ResourceType(), 0x03);
    ASSERT_EQ(handle.Generation(), 1);
}

NOLINT_TEST(ResourceHandleTest, CopyConstructor)
{
    const ResourceHandle handle1(1U, 0x04);
    const auto handle2(handle1);
    EXPECT_EQ(handle1, handle2);
}

NOLINT_TEST(ResourceHandleTest, CopyAssignment)
{
    const ResourceHandle handle1(1U, 0x04);
    const auto handle2 = handle1;
    EXPECT_EQ(handle1, handle2);
}

NOLINT_TEST(ResourceHandleTest, MoveConstructor)
{
    ResourceHandle handle1(1U, 0x04);
    const auto handle2(std::move(handle1));
    EXPECT_EQ(handle2.Index(), 1U);
    EXPECT_EQ(handle2.ResourceType(), 0x04);
    EXPECT_EQ(handle2.Generation(), 0);
    EXPECT_FALSE(handle1.IsValid()); // NOLINT(bugprone-use-after-move) for testing purposes
}

NOLINT_TEST(ResourceHandleTest, MoveAssignment)
{
    ResourceHandle handle1(1U, 0x04);
    const auto handle2 = std::move(handle1);

    EXPECT_EQ(handle2.Index(), 1U);
    EXPECT_EQ(handle2.ResourceType(), 0x04);
    EXPECT_EQ(handle2.Generation(), 0);
    EXPECT_FALSE(handle1.IsValid()); // NOLINT(bugprone-use-after-move) for testing purposes
}

NOLINT_TEST(ResourceHandleTest, Invalidate)
{
    ResourceHandle handle(1U, 0x04);
    ASSERT_TRUE(handle.IsValid());
    handle.Invalidate();
    ASSERT_FALSE(handle.IsValid());
}

} // namespace
