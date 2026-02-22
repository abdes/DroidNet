//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Shape/ShapeDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltShapeContractTest : public JoltTestFixture {
  protected:
    auto RequireBackend() -> void
    {
      AssertBackendAvailabilityContract();
      if (!HasBackend()) {
        GTEST_SKIP() << "No physics backend available.";
      }
    }
  };

} // namespace

NOLINT_TEST_F(JoltShapeContractTest, DestroyInvalidShapeReturnsError)
{
  RequireBackend();
  auto& shapes = System().Shapes();
  EXPECT_TRUE(shapes.DestroyShape(kInvalidShapeId).has_error());
}

NOLINT_TEST_F(JoltShapeContractTest, DestroyShapeTwiceReturnsError)
{
  RequireBackend();
  auto& shapes = System().Shapes();
  const auto create_result = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto shape_id = create_result.value();
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_error());
}

} // namespace oxygen::physics::test::jolt
