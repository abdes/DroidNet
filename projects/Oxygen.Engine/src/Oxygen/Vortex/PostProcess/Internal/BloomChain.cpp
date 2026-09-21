//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/PostProcess/Internal/BloomChain.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>

namespace oxygen::vortex::postprocess::internal {

auto BloomChain::ResolveOutput(
  const PostProcessFrameBindings& bindings) const noexcept -> Output
{
  // TODO(exposure, owned bloom): before activating downsample/upsample
  // dispatches, use checked SceneColor selection, scene-referred thresholds and
  // source P; inherit the view's HDR format and extend final image/meter error
  // bounds. Scope: design/vortex/lld/post-process-service.md, Quantization
  // error propagation; design/vortex/lld/scene-textures.md, HDR product 12.
  // Feature dependency: https://github.com/abdes/DroidNet/issues/12
  return {
    .ready = bindings.bloom_texture_srv != kInvalidShaderVisibleIndex,
    .bloom_texture_srv = bindings.bloom_texture_srv,
  };
}

} // namespace oxygen::vortex::postprocess::internal
