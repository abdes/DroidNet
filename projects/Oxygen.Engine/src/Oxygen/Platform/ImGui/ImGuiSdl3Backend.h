//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/api_export.h>

struct ImGuiContext;

namespace oxygen::platform {
class PlatformEvent;
} // namespace oxygen::platform

namespace oxygen::platform::imgui {

class ImGuiSdl3Backend final {
public:
  OXGN_PLAT_API ImGuiSdl3Backend(std::shared_ptr<Platform> platform,
    platform::WindowIdType window_id, ImGuiContext* imgui_context);

  OXGN_PLAT_API ~ImGuiSdl3Backend();

  OXYGEN_MAKE_NON_COPYABLE(ImGuiSdl3Backend)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiSdl3Backend)

  OXGN_PLAT_API auto NewFrame() -> void;

  // Coroutine that participates in platform event processing. This method
  // will be started by the Platform as an event filter so ImGui receives
  // the first opportunity to handle native events.
  OXGN_PLAT_API auto ProcessPlatformEvents(const platform::PlatformEvent& event)
    -> void;

private:
  std::shared_ptr<Platform> platform_;
  observer_ptr<ImGuiContext> imgui_context_;
};

} // namespace oxygen::platform::imgui
