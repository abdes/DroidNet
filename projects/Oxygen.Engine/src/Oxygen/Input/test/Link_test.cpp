//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/test/InputTestOwner.h>

using oxygen::input::MakeCodeAction;
using oxygen::input::testing::kTestInputOwner;

using oxygen::input::Action;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  const auto action = MakeCodeAction(
    kTestInputOwner, "a", oxygen::input::ActionValueType::kBool);
}
