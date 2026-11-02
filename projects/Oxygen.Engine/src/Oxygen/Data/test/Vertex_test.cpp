//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>

#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::AlmostEqual;
using oxygen::data::kVertexEpsilon;
using oxygen::data::Vertex;

constexpr Vertex kBase {
  .position = { 1.0F, 2.0F, 3.0F },
  .normal = { 0.0F, 1.0F, 0.0F },
  .texcoord = { 0.5F, 0.25F },
  .tangent = { 1.0F, 0.0F, 0.0F },
  .bitangent = { 0.0F, 0.0F, 1.0F },
  .color = { 0.9F, 0.8F, 0.7F, 1.0F },
};

NOLINT_TEST(VertexTest, EqualityIsExact)
{
  auto next = kBase;
  next.position.x = std::nextafter(kBase.position.x, 2.0F);

  EXPECT_EQ(kBase, Vertex { kBase });
  EXPECT_NE(kBase, next);
  EXPECT_TRUE(AlmostEqual(kBase, next));
}

NOLINT_TEST(VertexTest, EveryComponentTakesPartInEquality)
{
  const auto changed = [](auto change) -> Vertex {
    auto vertex = kBase;
    change(vertex);
    return vertex;
  };

  EXPECT_NE(kBase, changed([](Vertex& v) -> void { v.position.z += 1.0F; }));
  EXPECT_NE(kBase, changed([](Vertex& v) -> void { v.normal.y -= 1.0F; }));
  EXPECT_NE(kBase, changed([](Vertex& v) -> void { v.texcoord.y += 1.0F; }));
  EXPECT_NE(kBase, changed([](Vertex& v) -> void { v.tangent.x -= 1.0F; }));
  EXPECT_NE(kBase, changed([](Vertex& v) -> void { v.bitangent.z -= 1.0F; }));
  EXPECT_NE(kBase, changed([](Vertex& v) -> void { v.color.a -= 1.0F; }));
}

NOLINT_TEST(VertexTest, SignedZerosAreEqualAndNanEqualsNothing)
{
  auto negative_zero = kBase;
  negative_zero.normal.x = -0.0F;
  auto nan = kBase;
  nan.texcoord.x = std::numeric_limits<float>::quiet_NaN();

  EXPECT_EQ(kBase, negative_zero);
  EXPECT_NE(nan, nan);
  EXPECT_FALSE(AlmostEqual(nan, nan));
}

NOLINT_TEST(VertexTest, AlmostEqualHoldsUpToItsTolerance)
{
  auto within = kBase;
  within.position.y += 0.5F * kVertexEpsilon;
  auto beyond = kBase;
  beyond.color.g += 2.0F * kVertexEpsilon;

  EXPECT_TRUE(AlmostEqual(kBase, within));
  EXPECT_FALSE(AlmostEqual(kBase, beyond));
  EXPECT_TRUE(AlmostEqual(kBase, beyond, 4.0F * kVertexEpsilon));
}

//! Approximate equality is not transitive, which is why it is not `==`.
NOLINT_TEST(VertexTest, AlmostEqualIsNotTransitive)
{
  auto middle = kBase;
  middle.position.x += 0.75F * kVertexEpsilon;
  auto last = middle;
  last.position.x += 0.75F * kVertexEpsilon;

  EXPECT_TRUE(AlmostEqual(kBase, middle));
  EXPECT_TRUE(AlmostEqual(middle, last));
  EXPECT_FALSE(AlmostEqual(kBase, last));
}

} // namespace
