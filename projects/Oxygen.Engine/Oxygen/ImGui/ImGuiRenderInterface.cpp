//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/ImGui/ImGuiRenderInterface.h"

#include "Oxygen/Graphics/Common/CommandList.h"
#include "Oxygen/ImGui/ImGuiModule.h"

using oxygen::imgui::ImGuiRenderInterface;

ImGuiContext* ImGuiRenderInterface::GetContext() const
{
  DCHECK_NOTNULL_F(imgui_module_);
  return imgui_module_->GetImGuiContext();
}

auto ImGuiRenderInterface::Render(const graphics::Renderer* renderer) const -> graphics::CommandListPtr
{
  DCHECK_NOTNULL_F(imgui_module_);
  DCHECK_NOTNULL_F(renderer);

  if (imgui_module_ && renderer) {
    if (!new_frame_started_)
      throw std::runtime_error("Call NewFrame() before Render()");
    new_frame_started_ = true;
    return imgui_module_->ImGuiRender(renderer);
  }
  return {};
}

void ImGuiRenderInterface::NewFrame(const graphics::Renderer* renderer) const
{
  DCHECK_NOTNULL_F(imgui_module_);
  DCHECK_NOTNULL_F(renderer);

  if (new_frame_started_)
    return;
  if (imgui_module_ && renderer) {
    new_frame_started_ = true;
    imgui_module_->NewFrame(renderer);
  }
}
