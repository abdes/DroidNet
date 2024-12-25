//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Renderers/Common/CommandRecorder.h"
#include "Oxygen/Renderers/Direct3d12/CommandList.h"

namespace oxygen::renderer::d3d12 {

  class CommandRecorder final : public renderer::CommandRecorder
  {
    using Base = renderer::CommandRecorder;
  public:
    explicit CommandRecorder(const CommandListType type);
    ~CommandRecorder() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVEABLE(CommandRecorder);

    void Begin() override;
    auto End() -> CommandListPtr override;

  protected:
    void OnInitialize() override {};
    void OnRelease() override;

  private:
    std::unique_ptr<CommandList> current_command_list_;
  };

} // namespace oxygen::renderer::d3d12
