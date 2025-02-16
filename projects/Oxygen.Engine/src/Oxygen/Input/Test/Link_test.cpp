//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Input/Action.h>

using oxygen::input::Action;

auto main(int /*argc*/, char** /*argv*/) -> int
{
    const auto action = std::make_shared<Action>("a", oxygen::input::ActionValueType::kBool);
}
