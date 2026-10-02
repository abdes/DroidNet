//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/DemoModuleBase.h"
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

namespace oxygen::examples {

auto DemoModuleBase::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  auto* test = IM_REGISTER_TEST(engine, "demoshell", "runtime_smoke");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    const auto& app = *static_cast<DemoModuleBase*>(ctx->Test->UserData);
    constexpr int kWarmupFrames = 90;
    ctx->Yield(kWarmupFrames);
    IM_CHECK(app.ResolveVortexRenderer());
    IM_CHECK(!app.active_views_.empty());
    IM_CHECK(!app.scene_targets_.empty());
    for (const auto& [view, target] : app.scene_targets_) {
      static_cast<void>(view);
      IM_CHECK(target.scene_framebuffer && target.composite_framebuffer);
      IM_CHECK_GT(target.width, 0U);
      IM_CHECK_GT(target.height, 0U);
    }
  };
}

} // namespace oxygen::examples
