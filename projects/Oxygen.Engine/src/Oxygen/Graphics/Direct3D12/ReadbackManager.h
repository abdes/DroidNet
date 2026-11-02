//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/StagedReadbackManager.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class Graphics;

//! The D3D12 readback manager: multisampled textures resolve before their
//! copy, planar formats copy every plane, and readback surfaces map their
//! readback heap resource.
class D3D12ReadbackManager final : public graphics::StagedReadbackManager {
public:
  OXGN_D3D12_API explicit D3D12ReadbackManager(Graphics& graphics);
  OXGN_D3D12_API ~D3D12ReadbackManager() override;

  OXYGEN_MAKE_NON_COPYABLE(D3D12ReadbackManager)
  OXYGEN_MAKE_NON_MOVABLE(D3D12ReadbackManager)

protected:
  auto PrepareTextureReadbackCopy(graphics::CommandRecorder& recorder,
    const graphics::Texture& source, const graphics::TextureSlice& slice,
    graphics::TextureReadbackResolveTarget& resolve)
    -> std::expected<graphics::TextureReadbackCopy,
      graphics::ReadbackError> override;
  auto MapReadbackSurfaceBytes(
    graphics::Texture& surface, const graphics::TextureSlice& slice)
    -> std::expected<graphics::ReadbackSurfaceBytes,
      graphics::ReadbackError> override;
  auto UnmapReadbackSurfaceBytes(graphics::Texture& surface) -> void override;
};

} // namespace oxygen::graphics::d3d12
