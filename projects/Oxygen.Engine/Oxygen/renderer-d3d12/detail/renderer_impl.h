//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "Oxygen/renderer-d3d12/command.h"
#include "oxygen/renderer-d3d12/renderer.h"

namespace oxygen::renderer::direct3d12::detail {
  class RendererImpl
  {
  public:
    RendererImpl(PlatformPtr platform, const RendererProperties& props);
    ~RendererImpl() = default;
    OXYGEN_MAKE_NON_COPYABLE(RendererImpl);
    OXYGEN_MAKE_NON_MOVEABLE(RendererImpl);

    void Init();
    void Shutdown();
    void Render() const;

  private:
    PlatformPtr platform_;
    RendererProperties props_;
    Microsoft::WRL::ComPtr<ID3D12Device9> device_;
    std::unique_ptr<Command> command_{ nullptr };
  };

}  // namespace oxygen::renderer::direct3d12::detail
