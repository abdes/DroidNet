//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/EditorInterface/Api.h>

// NOLINTNEXTLINE
TEST(LinkedEditorApiTest, CanUseApi)
{
  // Call the function
  [[maybe_unused]] auto created = CreateScene("Test Scene");
}
