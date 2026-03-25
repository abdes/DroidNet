//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

namespace oxygen::engine::testing {

[[nodiscard]] inline auto DetectRendererWorkspaceRoot() -> std::filesystem::path
{
  auto current = std::filesystem::current_path();
  for (;;) {
    if (std::filesystem::exists(
          current / "src" / "Oxygen" / "Renderer" / "CMakeLists.txt")) {
      return current;
    }
    if (!current.has_parent_path() || current == current.parent_path()) {
      break;
    }
    current = current.parent_path();
  }

  throw std::runtime_error(
    "Failed to detect workspace root for renderer tests");
}

[[nodiscard]] inline auto EscapeJsonPath(
  const std::filesystem::path& path) -> std::string
{
  auto text = path.generic_string();
  std::string escaped;
  escaped.reserve(text.size());
  for (const char c : text) {
    if (c == '"') {
      escaped += "\\\"";
    } else {
      escaped += c;
    }
  }
  return escaped;
}

inline auto RunPass(RenderPass& pass, const RenderContext& render_context,
  graphics::CommandRecorder& recorder) -> void
{
  co::testing::TestEventLoop loop;
  co::Run(loop, [&]() -> co::Co<> {
    co_await pass.PrepareResources(render_context, recorder);
    co_await pass.Execute(render_context, recorder);
    co_return;
  });
}

class RendererOffscreenGpuTestFixture
  : public graphics::d3d12::testing::OffscreenTestFixture {
protected:
  [[nodiscard]] auto PathFinderConfigJson() const -> std::string override
  {
    return std::string { R"({"workspace_root_path":")" }
      + EscapeJsonPath(DetectRendererWorkspaceRoot()) + R"("})";
  }

  auto MakeRenderer() -> std::shared_ptr<Renderer>
  {
    RendererConfig renderer_config {};
    renderer_config.path_finder_config = PathFinderConfig::Create()
                                           .WithWorkspaceRoot(
                                             DetectRendererWorkspaceRoot())
                                           .Build();
    renderer_config.upload_queue_key
      = QueueKeyFor(graphics::QueueRole::kGraphics).get();

    graphics_ref_
      = std::shared_ptr<Graphics>(&Backend(), [](Graphics*) { });
    return std::make_shared<Renderer>(
      std::weak_ptr<Graphics>(graphics_ref_), std::move(renderer_config));
  }

private:
  std::shared_ptr<Graphics> graphics_ref_ {};
};

} // namespace oxygen::engine::testing
