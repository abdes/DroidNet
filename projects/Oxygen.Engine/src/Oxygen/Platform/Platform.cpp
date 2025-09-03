//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Platform/Platform.h>

using oxygen::Platform;
using oxygen::platform::AsyncOps;
using oxygen::platform::EventPump;
using oxygen::platform::InputEvents;
using oxygen::platform::WindowManager;

using namespace std::chrono_literals;

auto Platform::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  DLOG_F(INFO, "Platform Live Object activating...");
  return GetComponent<AsyncOps>().ActivateAsync(std::move(started));
}

auto Platform::Run() -> void
{
  if (!HasComponent<EventPump>()) {
    // This is a headless platform and will not have any coroutines
    return;
  }

  DLOG_F(INFO, "Starting Platform async tasks...");

  CHECK_F(GetComponent<AsyncOps>().IsRunning(),
    "Nursery must be opened via ActivateAsync before Run");

  auto& n = GetComponent<AsyncOps>().Nursery();
  n.Start(
    &WindowManager::ProcessPlatformEvents, &GetComponent<WindowManager>());
  n.Start(&InputEvents::ProcessPlatformEvents, &GetComponent<InputEvents>());
}

auto Platform::IsRunning() const -> bool
{
  return GetComponent<AsyncOps>().IsRunning();
}

auto Platform::Shutdown() -> co::Co<>
{
  // Shutdown the EventPump so it does not generate anymore platform events.
  if (HasComponent<EventPump>()) {
    Events().Shutdown();

    // Give a chance for all suspended coroutines to complete, by yielding to
    // the AsyncOps. The easiest way to do that is to just sleep for a tiny
    // amount of time.
    co_await Async().SleepFor(1us);

    DCHECK_F(!Events().IsRunning());
    DCHECK_F(!WindowManager().IsRunning());
    DCHECK_F(!InputEvents().IsRunning());
  }
}

auto Platform::Stop() -> void
{
  // Shutdown the EventPump before stopping to ensure all suspended coroutines
  // can complete their event processing naturally. This prevents crashes
  // during shutdown where coroutines are destroyed while holding semaphore
  // locks.
  if (HasComponent<EventPump>() && Events().IsRunning()) {
    auto& event_pump = GetComponent<EventPump>();
    event_pump.Shutdown();
  }

  GetComponent<AsyncOps>().Stop();
  DLOG_F(INFO, "Platform Live Object stopped");
}

auto Platform::Compose(const PlatformConfig& config) -> void
{
  AddComponent<AsyncOps>(config);

  if (config.headless) {
    LOG_F(INFO, "Platform is headless -> no input, no window");
    return;
  }
  AddComponent<EventPump>();
  AddComponent<WindowManager>();
  AddComponent<InputEvents>();
}

auto Platform::GetInputSlotForKey(const platform::Key key)
  -> platform::InputSlot
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
