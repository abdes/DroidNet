//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Common/Platform.h"

#include "Oxygen/Platform/Common/Input.h"

using oxygen::Platform;
using oxygen::platform::InputSlots;

Platform::Platform()
{
    InputSlots::Initialize();
}

// ReSharper disable CppMemberFunctionMayBeStatic

void Platform::GetAllInputSlots(std::vector<platform::InputSlot>& out_keys)
{
    InputSlots::GetAllInputSlots(out_keys);
}

auto Platform::GetInputSlotForKey(const platform::Key key) -> platform::InputSlot
{
    return InputSlots::GetInputSlotForKey(key);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto Platform::GetInputCategoryDisplayName(const std::string_view category_name)
    -> std::string_view
{
    return InputSlots::GetCategoryDisplayName(category_name);
}
