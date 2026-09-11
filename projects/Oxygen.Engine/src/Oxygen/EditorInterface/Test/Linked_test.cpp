//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/EditorInterface/Api.h>

using namespace oxygen::engine::interop;

namespace {

NOLINT_TEST(LinkedEditorApiTest, CanUseApi)
{
  // Call the function
  [[maybe_unused]] auto created = CreateScene("Test Scene");
}

NOLINT_TEST(LinkedEditorApiTest, StopWithoutContextIsHarmless)
{
  EXPECT_NO_THROW(StopEngine(nullptr));
}

NOLINT_TEST(LinkedEditorApiTest, StopWithoutEngineIsRepeatable)
{
  const auto context = std::make_shared<EngineContext>();
  EXPECT_NO_THROW(StopEngine(context));
  EXPECT_NO_THROW(StopEngine(context));
  EXPECT_TRUE(context->stop_requested.load());
}

} // namespace
