//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <Oxygen/Content/Tools/ImportTool/UI/Screens/ImportScreen.h>

namespace oxygen::content::import::tool {
namespace {
  using ftxui::color;
  using ftxui::Color;
  using ftxui::Element;
  using ftxui::Event;
  using ftxui::hbox;
  using ftxui::size;
  using ftxui::text;
  using ftxui::vbox;
  using ftxui::window;

  auto BuildLogs(const JobViewModel& state) -> Element
  {
    std::vector<Element> lines;
    lines.reserve(state.recent_logs.size());
    for (const auto& line : state.recent_logs) {
      lines.push_back(text(line));
    }
    return vbox(std::move(lines));
  }

  auto BuildHeader(const JobViewModel& state) -> Element
  {
    const std::string status
      = std::format("{} {:.0f}%", state.status, state.progress * 100.0f);
    return hbox({ text(std::format("Status: {}", status)) });
  }

} // namespace

ImportScreen::ImportScreen() = default;

void ImportScreen::SetDataProvider(DataProvider provider)
{
  provider_ = std::move(provider);
}

auto ImportScreen::GetStateSnapshot() const -> JobViewModel
{
  std::scoped_lock lock(state_mutex_);
  return state_;
}

void ImportScreen::UpdateState(JobViewModel state)
{
  std::scoped_lock lock(state_mutex_);
  state_ = std::move(state);
  if (state_.completed) {
    completed_.store(true);
  }
}

void ImportScreen::Run()
{
  if (!provider_) {
    return;
  }

  completed_.store(false);
  completed_signaled_.store(false);
  UpdateState(provider_());

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  std::atomic<bool> running { true };

  auto renderer = ftxui::Renderer([&]() -> Element {
    const auto state = GetStateSnapshot();
    return vbox({
      window(text("Oxygen Import Tool - Job"), BuildHeader(state)),
      window(text("Logs"),
        BuildLogs(state) | size(ftxui::HEIGHT, ftxui::LESS_THAN, 8))
        | ftxui::flex,
    });
  });

  auto root = ftxui::CatchEvent(renderer, [&](const Event& event) {
    if (event == Event::Custom) {
      return true;
    }
    if (completed_.load() && !event.is_mouse()) {
      if (!completed_signaled_.exchange(true)) {
        screen.Exit();
      }
      return true;
    }
    return false;
  });

  std::thread updater([&]() {
    while (running.load()) {
      if (provider_) {
        UpdateState(provider_());
      }
      screen.PostEvent(Event::Custom);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  screen.Loop(root);
  running.store(false);
  if (updater.joinable()) {
    updater.join();
  }
}

} // namespace oxygen::content::import::tool
