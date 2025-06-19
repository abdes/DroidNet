//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Scene/SceneTraversalAsync.h>

#include "./SimpleEventLoop.h"

using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::scene::AsyncSceneTraversal;
using oxygen::scene::Scene;
using oxygen::scene::VisitResult;

using oxygen::scene::testing::SimpleEventLoop;

namespace {

class SceneTraversalAsyncTest : public testing::Test {
protected:
  std::unique_ptr<SimpleEventLoop> el_ {};

  void SetUp() override { el_ = std::make_unique<SimpleEventLoop>(); }

  void TearDown() override { el_.reset(); }
};

NOLINT_TEST_F(SceneTraversalAsyncTest, BasicTraversal)
{
  ::Run(*el_, []() -> Co<> {
    auto scene = std::make_shared<Scene>("TestScene");
    AsyncSceneTraversal traversal(scene);

    // Create a root node
    auto root_node = scene->CreateNode("Root");
    auto child_node = scene->CreateChildNode(root_node, "Child");

    // Traverse the scene with a simple visitor
    auto result = co_await traversal.TraverseAsync(
      [](const auto& node, bool dry_run) -> Co<VisitResult> {
        if (!dry_run) {
          // In a real scenario, you might do something with the node
          std::cerr << "Visiting node: " << node.node_impl->GetName() << '\n';
        }
        co_return VisitResult::kContinue;
      });

    EXPECT_TRUE(result.completed);
    co_return;
  });
}

} // namespace
