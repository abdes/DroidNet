//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Renderers/Common/Disposable.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen::renderer {

  class CommandList : public Disposable
  {
  public:
    CommandList() = default;
    ~CommandList() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandList);
    OXYGEN_DEFAULT_MOVABLE(CommandList);

    void Initialize(const CommandListType type)
    {
      Release();
      OnInitialize(type);
      ShouldRelease(true);
    }

    [[nodiscard]] virtual auto GetQueueType() const -> CommandListType { return type_; }

  protected:
    virtual void OnInitialize(CommandListType type) = 0;

  private:
    CommandListType type_{ CommandListType::kNone };
  };

} // namespace oxygen::renderer
