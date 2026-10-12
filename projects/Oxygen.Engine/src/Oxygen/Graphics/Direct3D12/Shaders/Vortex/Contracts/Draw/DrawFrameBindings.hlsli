//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI

#include "Vortex/Contracts/Draw/Generated.DrawContracts.hlsli"

// The view's draw routing record; all slots invalid when `slot` is outside the
// global SRV domain.
static DrawFrameBindings LoadDrawFrameBindings(uint slot)
{
    DrawFrameBindings bindings;
    TryLoadDrawFrameBindings(slot, bindings);
    return bindings;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI
