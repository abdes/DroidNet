//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/ObjectRelease.h>

#include <memory>

#include <gmock/gmock.h>

#include <Oxygen/Testing/GTest.h>

using oxygen::graphics::ObjectRelease;

namespace {

// Mock class with a Release() method
class MockReleasable {
public:
  // ReSharper disable CppDeclaratorNeverUsed
  // ReSharper disable CppMemberFunctionMayBeConst
  MOCK_METHOD(void, Release, ());
  // ReSharper restore CppMemberFunctionMayBeConst
  // ReSharper restore CppDeclaratorNeverUsed
};

// Test case for ObjectRelease with a raw pointer to an object with a Release()
// method
NOLINT_TEST(ObjectReleaseTest, ReleasesObjectWithReleaseMethod)
{
  // Arrange
  auto mock_owner = std::make_unique<MockReleasable>();
  auto* mock_object = mock_owner.get();
  EXPECT_CALL(*mock_object, Release()).Times(1);

  // Act
  ObjectRelease(mock_object);

  // Assert
  EXPECT_EQ(mock_object, nullptr);
  // mock_owner will clean up the object
}

// Test case for ObjectRelease with a raw pointer that is already null
NOLINT_TEST(ObjectReleaseTest, DoesNothingForNullPointerWithReleaseMethod)
{
  // Arrange
  MockReleasable* mock_object = nullptr;

  // Act
  ObjectRelease(mock_object);

  // Assert
  EXPECT_EQ(mock_object, nullptr);
}

// Mock class with a destructor
struct MockObject { // NOLINT(cppcoreguidelines-special-member-functions)
  static inline bool destructor_called = false;
  ~MockObject() { destructor_called = true; }
};

// Test case for ObjectRelease with a shared_ptr
NOLINT_TEST(ObjectReleaseTest, ReleasesSharedPtrCallsObjectDestructor)
{
  MockObject::destructor_called = false; // Reset the flag
  auto shared = std::make_shared<MockObject>();

  // Act
  ObjectRelease(shared);

  // Assert
  EXPECT_EQ(shared, nullptr);
  EXPECT_TRUE(MockObject::destructor_called); // Ensure destructor was called
}

// Test case for ObjectRelease with a shared_ptr that is already null
NOLINT_TEST(ObjectReleaseTest, DoesNothingForNullSharedPtr)
{
  // Arrange
  std::shared_ptr<int> shared;

  // Act
  ObjectRelease(shared);

  // Assert
  EXPECT_EQ(shared, nullptr);
}

} // namespace
