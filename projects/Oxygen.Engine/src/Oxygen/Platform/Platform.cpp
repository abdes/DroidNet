//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Platform/Platform.h>

using oxygen::Platform;
using oxygen::platform::AsyncOps;
using oxygen::platform::EventPump;
using oxygen::platform::InputEvents;
using oxygen::platform::WindowManager;

void Platform::Run()
{
    auto& n = GetComponent<AsyncOps>().Nursery();

    if (HasComponent<EventPump>()) {
        n.Start(&WindowManager::ProcessPlatformEvents, &GetComponent<WindowManager>());
    }
    if (HasComponent<InputEvents>()) {
        n.Start(&InputEvents::ProcessPlatformEvents, &GetComponent<InputEvents>());
    }
}

void Platform::Compose(const PlatformConfig& config)
{
    AddComponent<AsyncOps>();

    if (config.headless) {
        LOG_F(INFO, "Platform is headless -> no input, no window");
        return;
    }
    AddComponent<EventPump>();
    AddComponent<WindowManager>();
    AddComponent<InputEvents>();
}

auto Platform::GetInputSlotForKey(const platform::Key key) -> platform::InputSlot
{
    return platform::InputSlots::GetInputSlotForKey(key);
}

#if 0
#  include <Oxygen/Platform/Input.h>

using oxygen::PlatformBase;
using oxygen::platform::InputSlots;

PlatformBase::PlatformBase()
{
    InputSlots::Initialize();
}

// ReSharper disable CppMemberFunctionMayBeStatic

void PlatformBase::GetAllInputSlots(std::vector<platform::InputSlot>& out_keys)
{
    InputSlots::GetAllInputSlots(out_keys);
}

auto PlatformBase::GetInputSlotForKey(const platform::Key key) -> platform::InputSlot
{
    return InputSlots::GetInputSlotForKey(key);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto PlatformBase::GetInputCategoryDisplayName(const std::string_view category_name)
    -> std::string_view
{
    return InputSlots::GetCategoryDisplayName(category_name);
}

#endif
