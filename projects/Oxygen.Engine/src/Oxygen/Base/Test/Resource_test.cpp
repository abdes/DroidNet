//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Resource.h>

using oxygen::Resource;
using oxygen::ResourceHandle;

namespace base::resources {

constexpr ResourceHandle::ResourceTypeT kTestResourceType = 0x01;
class TestResource final : public Resource<kTestResourceType> {
public:
  using Resource::Resource;

  void InvalidateResource() { Invalidate(); }
};

NOLINT_TEST(ResourceTest, DefaultConstructor)
{
  const TestResource resource;
  EXPECT_FALSE(resource.IsValid());
}

NOLINT_TEST(ResourceTest, ParameterizedConstructor)
{
  const ResourceHandle handle(1U, kTestResourceType);
  const TestResource resource(handle);
  EXPECT_TRUE(resource.IsValid());
  EXPECT_EQ(resource.GetHandle(), handle);
}

NOLINT_TEST(ResourceTest, CopyConstructor)
{
  const ResourceHandle handle(1U, kTestResourceType);
  const TestResource resource1(handle);
  const auto& resource2(resource1);
  EXPECT_EQ(resource1.GetHandle(), resource2.GetHandle());
}

NOLINT_TEST(ResourceTest, CopyAssignment)
{
  const ResourceHandle handle(1U, kTestResourceType);
  const TestResource resource1(handle);
  const auto& resource2 = resource1;
  EXPECT_EQ(resource1.GetHandle(), resource2.GetHandle());
}

NOLINT_TEST(ResourceTest, MoveConstructor)
{
  const ResourceHandle handle(1U, kTestResourceType);
  TestResource resource1(handle);
  const auto resource2(std::move(resource1));
  EXPECT_EQ(resource2.GetHandle(), handle);
  EXPECT_FALSE(resource1
      .IsValid()); // NOLINT(bugprone-use-after-move) for testing purposes
}

NOLINT_TEST(ResourceTest, MoveAssignment)
{
  const ResourceHandle handle(1U, kTestResourceType);
  TestResource resource1(handle);
  const auto resource2 = std::move(resource1);
  EXPECT_EQ(resource2.GetHandle(), handle);
  EXPECT_FALSE(resource1
      .IsValid()); // NOLINT(bugprone-use-after-move) for testing purposes
}

NOLINT_TEST(ResourceTest, Invalidate)
{
  const ResourceHandle handle(1U, kTestResourceType);
  TestResource resource(handle);
  EXPECT_TRUE(resource.IsValid());
  resource.InvalidateResource();
  EXPECT_FALSE(resource.IsValid());
}

} // namespace base::resources
