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

  class JoltShapeBasicTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltShapeBasicTest, CreateAndDestroyShapeSucceeds)
{
  RequireBackend();

  auto& shapes = System().Shapes();
  const auto create_result = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(create_result.has_value());
  const auto shape_id = create_result.value();
  EXPECT_NE(shape_id, kInvalidShapeId);
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
}

} // namespace oxygen::physics::test::jolt
