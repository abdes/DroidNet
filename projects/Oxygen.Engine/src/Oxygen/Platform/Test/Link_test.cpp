//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Platform/Input.h>

using oxygen::platform::InputSlots;

auto main(int /*argc*/, char** /*argv*/) -> int
{
  InputSlots::Initialize();
  const auto input_slot
    = InputSlots::GetInputSlotForKey(oxygen::platform::Key::kHash);
  std::cout << "Input slot: " << input_slot.GetName() << "\n";
}
