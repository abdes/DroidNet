//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/ImGui/ImGuiPass.h>

using oxygen::imgui::ImGuiPass;

ImGuiPass::ImGuiPass(std::shared_ptr<ImGuiGraphicsBackend> backend)
  : backend_(std::move(backend))
{
  DCHECK_NOTNULL_F(backend_);
}

auto ImGuiPass::Render(graphics::CommandRecorder& recorder) const -> co::Co<>
{
  if (disabled_) {
    co_return;
  }

  backend_->Render(recorder);
  co_return;
}
