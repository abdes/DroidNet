//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Graphics/Common/Types.h"
#include "Oxygen/api_export.h"

struct ImGuiContext;
struct ImDrawData;

namespace oxygen::imgui {

class ImguiModule;

class ImGuiRenderInterface
{
 public:
  OXYGEN_API ImGuiRenderInterface() = default;
  OXYGEN_API ~ImGuiRenderInterface() = default;
  OXYGEN_API ImGuiContext* GetContext() const;

  OXYGEN_DEFAULT_COPYABLE(ImGuiRenderInterface);
  OXYGEN_DEFAULT_MOVABLE(ImGuiRenderInterface);

  OXYGEN_API auto Render(const graphics::Renderer* renderer) const -> graphics::CommandListPtr;
  OXYGEN_API void NewFrame(const graphics::Renderer* renderer) const;

 private:
  friend class ImguiModule;
  explicit ImGuiRenderInterface(ImguiModule* imgui_module)
    : imgui_module_(imgui_module)
  {
  }

  ImguiModule* imgui_module_ { nullptr };
  mutable bool new_frame_started_ { false };
};

} // namespace oxygen::imgui
