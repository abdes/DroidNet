//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/PostProcess/Internal/BloomChain.h>

namespace oxygen::vortex::postprocess::internal {

auto BloomChain::ResolveOutput(
  const PostProcessFrameBindings& bindings) const noexcept -> Output
{
  return {
    .ready = bindings.bloom_texture_srv != kInvalidShaderVisibleIndex,
    .bloom_texture_srv = bindings.bloom_texture_srv,
  };
}

} // namespace oxygen::vortex::postprocess::internal
