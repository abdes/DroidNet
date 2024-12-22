//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <d3d12.h>

#include "oxygen/base/macros.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

namespace oxygen::renderer::d3d12 {

  namespace detail {
    class CommanderImpl;
  }

  class Commander final
  {
  public:
    explicit Commander(DeviceType* device, D3D12_COMMAND_LIST_TYPE type);
    ~Commander();

    OXYGEN_MAKE_NON_COPYABLE(Commander);
    OXYGEN_MAKE_NON_MOVEABLE(Commander);

    void Release() const noexcept;

    [[nodiscard]] CommandQueueType* CommandQueue() const noexcept;
    [[nodiscard]] GraphicsCommandListType* CommandList() const noexcept;
    [[nodiscard]] size_t CurrentFrameIndex() const noexcept;

    void BeginFrame() const;
    void EndFrame() const;

    void Flush() const;

  private:
    std::unique_ptr<detail::CommanderImpl> pimpl_;
  };

}  // namespace oxygen::renderer::d3d12
