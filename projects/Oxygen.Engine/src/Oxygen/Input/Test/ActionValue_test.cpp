//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::input::ActionValue;
using oxygen::input::ActionValueType;

namespace {

//! Tests construction and GetAs for the three supported value types.
NOLINT_TEST(ActionValue, ConstructionAndGetAs)
{
  // Arrange
  ActionValue b { true };
  ActionValue a1 { oxygen::Axis1D { 0.5F } };
  ActionValue a2 { oxygen::Axis2D { 0.25F, -0.75F } };

  // Act & Assert
  EXPECT_TRUE(b.GetAs<bool>());
  EXPECT_FLOAT_EQ(a1.GetAs<oxygen::Axis1D>().x, 0.5F);
  EXPECT_FLOAT_EQ(a2.GetAs<oxygen::Axis2D>().x, 0.25F);
  EXPECT_FLOAT_EQ(a2.GetAs<oxygen::Axis2D>().y, -0.75F);
}

//! Tests Set() overloads change the stored value.
NOLINT_TEST(ActionValue, SetOverrides)
{
  // Arrange
  ActionValue v { false };

  // Act
  v.Set(oxygen::Axis1D { 1.0F });

  // Assert
  EXPECT_FLOAT_EQ(v.GetAs<oxygen::Axis1D>().x, 1.0F);

  // Act
  v.Set(oxygen::Axis2D { 0.5F, 0.6F });

  // Assert
  EXPECT_FLOAT_EQ(v.GetAs<oxygen::Axis2D>().x, 0.5F);
  EXPECT_FLOAT_EQ(v.GetAs<oxygen::Axis2D>().y, 0.6F);

  // Act
  v.Set(true);

  // Assert
  EXPECT_TRUE(v.GetAs<bool>());
}

//! Tests Update(bool) semantics when stored type is bool/Axis1D/Axis2D.
NOLINT_TEST(ActionValue, UpdateFromBool)
{
  // Arrange
  ActionValue vb { false };
  ActionValue v1 { oxygen::Axis1D { 0.0F } };
  ActionValue v2 { oxygen::Axis2D { 0.0F, 0.0F } };

  // Act
  vb.Update(true);
  v1.Update(true);
  v2.Update(true);

  // Assert
  EXPECT_TRUE(vb.GetAs<bool>());
  EXPECT_FLOAT_EQ(v1.GetAs<oxygen::Axis1D>().x, 1.0F);
  EXPECT_FLOAT_EQ(v2.GetAs<oxygen::Axis2D>().x, 1.0F);
}

//! Tests Update(Axis1D) semantics for different stored types.
NOLINT_TEST(ActionValue, UpdateFromAxis1D)
{
  // Arrange
  ActionValue vb { false };
  ActionValue v1 { oxygen::Axis1D { 0.0F } };
  ActionValue v2 { oxygen::Axis2D { 0.0F, 0.0F } };

  // Act
  const oxygen::Axis1D src { 0.42F };
  vb.Update(src);
  v1.Update(src);
  v2.Update(src);

  // Assert
  EXPECT_TRUE(vb.GetAs<bool>());
  EXPECT_FLOAT_EQ(v1.GetAs<oxygen::Axis1D>().x, 0.42F);
  EXPECT_FLOAT_EQ(v2.GetAs<oxygen::Axis2D>().x, 0.42F);
}

//! Tests Update(Axis2D) semantics for different stored types.
NOLINT_TEST(ActionValue, UpdateFromAxis2D)
{
  // Arrange
  ActionValue vb { false };
  ActionValue v1 { oxygen::Axis1D { 0.0F } };
  ActionValue v2 { oxygen::Axis2D { 0.0F, 0.0F } };

  // Act
  const oxygen::Axis2D src { 0.3F, -0.6F };
  vb.Update(src);
  v1.Update(src);
  v2.Update(src);

  // Assert
  EXPECT_TRUE(vb.GetAs<bool>());
  EXPECT_FLOAT_EQ(v1.GetAs<oxygen::Axis1D>().x, 0.3F);
  EXPECT_FLOAT_EQ(v2.GetAs<oxygen::Axis2D>().x, 0.3F);
  EXPECT_FLOAT_EQ(v2.GetAs<oxygen::Axis2D>().y, -0.6F);
}

//! Tests IsActuated threshold behavior across types and boundary values.
NOLINT_TEST(ActionValue, IsActuatedThresholds)
{
  // Arrange
  ActionValue bfalse { false };
  ActionValue btrue { true };
  ActionValue a1 { oxygen::Axis1D { 0.1F } };
  ActionValue a2 { oxygen::Axis2D { 0.05F, 0.06F } };

  // Act & Assert
  EXPECT_FALSE(bfalse.IsActuated(0.5F));
  EXPECT_TRUE(btrue.IsActuated(0.5F));
  EXPECT_TRUE(a1.IsActuated(0.05F));
  EXPECT_FALSE(a2.IsActuated(0.1F));

  // Boundary: exactly threshold should be false since check is '>'
  ActionValue a_edge { oxygen::Axis1D { 0.5F } };
  EXPECT_FALSE(a_edge.IsActuated(0.5F));
}

} // namespace
