//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>

namespace {

using oxygen::ObjectMetaData;

// Constructor Tests
NOLINT_TEST(ObjectMetaDataTest, Constructor)
{
    const ObjectMetaData md("TestObject");
    EXPECT_EQ(md.GetName(), "TestObject");
}

// Setter Tests
NOLINT_TEST(ObjectMetaDataTest, SetName)
{
    ObjectMetaData md("TestObject");
    md.SetName("NewName");
    EXPECT_EQ(md.GetName(), "NewName");
}

// Clone Tests
NOLINT_TEST(ObjectMetaDataTest, Clone)
{
    const ObjectMetaData original("TestObject");
    const auto cloned = original.Clone();
    auto* cloned_meta_data = reinterpret_cast<ObjectMetaData*>(cloned.get()); // NOLINT(*-pro-type-reinterpret-cast)
    ASSERT_NE(cloned_meta_data, nullptr);
    EXPECT_EQ(cloned_meta_data->GetName(), "TestObject");
    cloned_meta_data->SetName("NewName");
    EXPECT_EQ(original.GetName(), "TestObject");
    EXPECT_EQ(cloned_meta_data->GetName(), "NewName");
}

// Copy/Move Tests
NOLINT_TEST(ObjectMetaDataTest, CopyConstructor)
{
    const ObjectMetaData original("TestObject");
    ObjectMetaData copy(original);
    EXPECT_EQ(copy.GetName(), "TestObject");
    copy.SetName("CopiedObject");
    EXPECT_EQ(copy.GetName(), "CopiedObject");
    EXPECT_EQ(original.GetName(), "TestObject");
}

NOLINT_TEST(ObjectMetaDataTest, MoveConstructor)
{
    ObjectMetaData original("TestObject");
    const ObjectMetaData moved(std::move(original));
    EXPECT_EQ(moved.GetName(), "TestObject");
}

NOLINT_TEST(ObjectMetaDataTest, CopyAssignment)
{
    const ObjectMetaData original("TestObject");
    ObjectMetaData copy = original;
    EXPECT_EQ(copy.GetName(), "TestObject");
    copy.SetName("CopiedObject");
    EXPECT_EQ(copy.GetName(), "CopiedObject");
    EXPECT_EQ(original.GetName(), "TestObject");
}

NOLINT_TEST(ObjectMetaDataTest, MoveAssignment)
{
    ObjectMetaData original("TestObject");
    const ObjectMetaData moved = std::move(original);
    EXPECT_EQ(moved.GetName(), "TestObject");
}

} // namespace
