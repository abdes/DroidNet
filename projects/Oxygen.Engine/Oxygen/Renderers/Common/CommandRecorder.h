//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "glm/vec4.hpp"

#include "Oxygen/Renderers/Common/Disposable.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen::renderer {

  class CommandRecorder : public Disposable
  {
  public:
    explicit CommandRecorder(const CommandListType type)
      : type_{ type }
    {
    }
    ~CommandRecorder() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVEABLE(CommandRecorder);

    void Initialize()
    {
      Release();
      OnInitialize();
      ShouldRelease(true);
    }

    [[nodiscard]] virtual auto GetQueueType() const -> CommandListType { return type_; }

    virtual void Begin() = 0;
    virtual auto End() -> CommandListPtr = 0;

    // Graphics commands
    virtual void Clear(uint32_t flags, uint32_t numTargets, const uint32_t* slots, const glm::vec4* colors, float depthValue, uint8_t stencilValue) = 0;

  protected:
    virtual void OnInitialize() = 0;

  private:
    CommandListType type_{ CommandListType::kNone };
  };

} // namespace oxygen::renderer
