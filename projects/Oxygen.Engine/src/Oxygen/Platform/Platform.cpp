//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Platform/Platform.h>

using namespace std::chrono_literals;

namespace oxygen {

using namespace platform;

auto Platform::FilterPlatformEvents() -> co::Co<>
{
  // Keep a shared_ptr to the platform for the coroutine lifetime
  while (Async().IsRunning()) {
    // Check if the event pump is still running. If not, the next event is a
    // dummy one that we should just ignore, and this loop should immediately
    // terminate.
    if (!Events().IsRunning()) {
      break;
    }

    auto& event = co_await Events().NextEvent();
    // If we do not have an installed filter, just continue
    if (!event_filter_) {
      continue;
    }

    // Acquire the event pump lock to cooperate with other processors and
    // ensure ordered, sequential handling (same pattern as WindowManager
    // and InputEvents).
    auto _ = co_await Events().Lock();
    // If event already handled, skip
    if (event.IsHandled()) {
      continue;
    }
    // Invoke the filtering callable
    event_filter_(event);
  }
  DLOG_F(INFO, "DONE: platform event filter");
  co_return;
}

auto Platform::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  DLOG_F(1, "Platform Live Object activating...");
  return GetComponent<AsyncOps>().ActivateAsync(std::move(started));
}

auto Platform::Run() -> void
{
  if (!HasComponent<EventPump>()) {
    // This is a headless platform and will not have any coroutines
    return;
  }
  DLOG_SCOPE_F(INFO, "Starting Platform async tasks");

  CHECK_F(GetComponent<AsyncOps>().IsRunning(),
    "Nursery must be opened via ActivateAsync before Run");

  auto& n = GetComponent<AsyncOps>().Nursery();
  // Start the event filter first so it has priority handling over platform
  // events (ImGui requires first-pass access).
  DLOG_F(INFO, "-> event filter");
  n.Start(&Platform::FilterPlatformEvents, this);
  DLOG_F(INFO, "-> window manager");
  n.Start(
    &WindowManager::ProcessPlatformEvents, &GetComponent<WindowManager>());
  DLOG_F(INFO, "-> input events producer");
  n.Start(&InputEvents::ProcessPlatformEvents, &GetComponent<InputEvents>());
}

auto Platform::IsRunning() const -> bool
{
  return GetComponent<AsyncOps>().IsRunning();
}

auto Platform::Shutdown() -> co::Co<>
{
  DLOG_F(INFO, "Platform immediate shutdown...");

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

auto Platform::OnFrameStart() -> void
{
  if (HasComponent<WindowManager>()) {
    auto& window_manager = Windows();
    // Allow registered consumers (e.g., engine modules) to react to the
    // pending window closures before we actually tear down native windows.
    // This prevents races where those modules hold pointers into the native
    // window object and would crash if the native window was destroyed first.
    const auto& pending = window_manager.GetPendingCloses();
    if (!pending.empty()) {
      // Copy handlers to avoid mutation during invocation. Handlers may
      // unregister themselves which would otherwise modify the map we are
      // iterating.
      const auto handlers = window_about_to_be_destroyed_handlers_;
      for (const auto& kv : handlers) {
        const auto& handler = kv.second;
        for (const auto window_id : pending) {
          try {
            handler(window_id);
          } catch (const std::exception& ex) {
            LOG_F(ERROR, "Window destroy handler threw: {}", ex.what());
          }
        }
      }
    }

    // Process windows queued for closing. Perform native window teardown at
    // the end of the frame so modules using window resources during the
    // frame are not disrupted.
    window_manager.ProcessPendingCloses();
  }
}

auto Platform::OnFrameEnd() -> void
{
  if (HasComponent<WindowManager>()) {
    auto& window_manager = Windows();
    // Scan for new windows that are pending close and queue them for next frame
    // We do NOT destroy the native window yet; destruction happens at
    // OnFrameEnd so the native window remains valid during the frame.
    window_manager.ScanForPendingCloses();
  }
}

auto Platform::RegisterWindowAboutToBeDestroyedHandler(
  WindowAboutToBeDestroyedHandler handler) -> size_t
{
  const auto id = window_about_to_be_destroyed_next_id_.fetch_add(1);
  window_about_to_be_destroyed_handlers_.emplace(id, std::move(handler));
  return id;
}

void Platform::UnregisterWindowAboutToBeDestroyedHandler(size_t token)
{
  window_about_to_be_destroyed_handlers_.erase(token);
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

} // namespace oxygen::platform
