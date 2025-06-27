//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/RenderGraph.h>
#include <Oxygen/Renderer/RenderPass.h>

using oxygen::graphics::Color;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Scissors;
using oxygen::graphics::ViewPort;

namespace oxygen::engine {

// Generic no-op implementation for any render pass type.
class NullRenderPass final : public RenderPass {
public:
  explicit NullRenderPass(const std::string_view name = "NullRenderPass")
    : RenderPass(name)
  {
  }

  ~NullRenderPass() noexcept override = default;

  OXYGEN_DEFAULT_COPYABLE(NullRenderPass)
  OXYGEN_DEFAULT_MOVABLE(NullRenderPass)

  auto PrepareResources(CommandRecorder&) -> co::Co<> override { co_return; }
  auto Execute(CommandRecorder&) -> co::Co<> override { co_return; }
  auto SetViewport(const ViewPort&) -> void override { }
  auto SetScissors(const Scissors&) -> void override { }
  auto SetClearColor(const Color&) -> void override { }
  auto SetEnabled(bool) -> void override { }
  auto IsEnabled() const -> bool override { return false; }
  auto GetName() const noexcept -> std::string_view override { return name_; }
  auto SetName(const std::string_view name) noexcept -> void override
  {
    name_ = std::string(name);
  }

private:
  std::string name_;
};

} // namespace oxygen::engine

using oxygen::engine::RenderGraph;
using oxygen::engine::RenderPass;

// ReSharper disable once CppMemberFunctionMayBeStatic
auto RenderGraph::CreateNullRenderPass() -> std::shared_ptr<RenderPass>
{
  return std::make_shared<NullRenderPass>();
}

auto RenderGraph::CreateDepthPrePass(std::shared_ptr<DepthPrePassConfig> config)
  -> std::shared_ptr<RenderPass>
{
  return std::static_pointer_cast<RenderPass>(
    std::make_shared<DepthPrePass>(this, std::move(config)));
}
