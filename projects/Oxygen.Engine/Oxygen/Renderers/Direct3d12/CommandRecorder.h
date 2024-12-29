//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Renderers/Common/CommandRecorder.h"
#include "Oxygen/Renderers/Direct3d12/CommandList.h"
#include "Oxygen/Renderers/Direct3d12/ResourceStateCache.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12 {

  class CommandRecorder final : public renderer::CommandRecorder
  {
    using Base = renderer::CommandRecorder;
  public:
    explicit CommandRecorder(const CommandListType type)
      : Base(type)
    {

    }
    ~CommandRecorder() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVEABLE(CommandRecorder);

    void Begin() override;
    auto End() -> CommandListPtr override;

    void Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors, float depth_value, uint8_t stencil_value) override;
    void SetRenderTarget(RenderTargetNoDeletePtr render_target); // TODO: push up to base class

  protected:
    void InitializeCommandRecorder() override {}
    void ReleaseCommandRecorder() noexcept override;

  private:
    void ResetState();

    std::unique_ptr<CommandList> current_command_list_;

    ResourceStateCache resource_state_cache_{};

    RenderTargetNoDeletePtr current_render_target_{ nullptr };
  };

} // namespace oxygen::renderer::d3d12
