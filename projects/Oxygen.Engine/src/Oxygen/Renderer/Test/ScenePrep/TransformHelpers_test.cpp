//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/State/TransformManager.h>

#include <glm/glm.hpp>

using namespace oxygen::engine::sceneprep;

namespace {

NOLINT_TEST(TransformHelpers, TransformManager_DeduplicatesAndFlushes)
{
  TransformManager mgr;

  const glm::mat4 t0 = glm::mat4(1.0f);
  const glm::mat4 t1 = glm::mat4(1.0f); // identical to t0
  const glm::mat4 t2 = glm::mat4(1.0f) * 2.0f; // different

  auto h0 = mgr.GetOrAllocate(t0);
  auto h1 = mgr.GetOrAllocate(t1);
  auto h2 = mgr.GetOrAllocate(t2);

  // Deduplication: t0 and t1 must share the same handle
  EXPECT_EQ(h0.get(), h1.get());
  EXPECT_NE(h0.get(), h2.get());

  // Unique transforms count must be 2
  EXPECT_EQ(mgr.GetUniqueTransformCount(), 2u);

  // Validity and retrieval
  EXPECT_TRUE(mgr.IsValidHandle(h0));
  EXPECT_TRUE(mgr.IsValidHandle(h2));
  EXPECT_EQ(mgr.GetTransform(h0), t0);
  EXPECT_EQ(mgr.GetTransform(h2), t2);

  // Invalid handle returns identity
  auto invalid = TransformHandle { 9999U };
  EXPECT_FALSE(mgr.IsValidHandle(invalid));
  EXPECT_EQ(mgr.GetTransform(invalid), glm::mat4(1.0f));

  // Flush should be a no-op from API perspective (no crash, counts preserved)
  mgr.FlushPendingUploads();
  EXPECT_EQ(mgr.GetUniqueTransformCount(), 2u);
}

} // namespace
