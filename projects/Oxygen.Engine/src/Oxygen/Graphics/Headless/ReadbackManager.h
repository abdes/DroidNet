//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/StagedReadbackManager.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {

class Graphics;

//! The headless readback manager: copies land in CPU-backed staging buffers,
//! and readback surfaces map their CPU backing.
class HeadlessReadbackManager final : public graphics::StagedReadbackManager {
public:
  OXGN_HDLS_API explicit HeadlessReadbackManager(Graphics& graphics);
  OXGN_HDLS_API ~HeadlessReadbackManager() override;

  OXYGEN_MAKE_NON_COPYABLE(HeadlessReadbackManager)
  OXYGEN_MAKE_NON_MOVABLE(HeadlessReadbackManager)

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

} // namespace oxygen::graphics::headless
