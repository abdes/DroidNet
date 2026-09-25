//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <filesystem>
#include <string>

#include <Oxygen/Base/Macros.h>

struct ImGuiContext;
struct ImGuiTestEngine;

namespace oxygen::examples::testing {

// Compiled only with OXYGEN_BUILD_UI_TESTS. Owns test hooks, not the app
// context.
class UiTestSession final {
public:
  static auto Requested() -> bool;
  static auto ExitCode(int application_exit_code) -> int;
  UiTestSession(ImGuiContext& context, void* native_window);
  ~UiTestSession();
  OXYGEN_MAKE_NON_COPYABLE(UiTestSession)
  OXYGEN_MAKE_NON_MOVABLE(UiTestSession)

  auto Engine() const -> ImGuiTestEngine* { return engine_; }
  auto Start() -> void;
  // Called after actual presentation. True means the queue finished.
  auto AfterPresent() -> bool;
  auto OutputDirectory() const -> const std::filesystem::path&
  {
    return output_;
  }

private:
  ImGuiTestEngine* engine_ {};
  std::filesystem::path output_;
  std::string report_;
  std::string pending_failure_;
  void* native_window_ {};
  unsigned failure_index_ {};
  bool started_ {};
  bool finished_ {};
};
} // namespace oxygen::examples::testing
