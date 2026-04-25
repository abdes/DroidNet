//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Vortex/PostProcess/Internal/BloomChain.h>
#include <Oxygen/Vortex/PostProcess/Passes/BloomPass.h>

namespace oxygen::vortex::postprocess {

BloomPass::BloomPass(Renderer& renderer)
  : renderer_(renderer)
  , bloom_chain_(std::make_unique<internal::BloomChain>())
{
}

BloomPass::~BloomPass() = default;

auto BloomPass::Execute(const PostProcessConfig& config,
  const PostProcessFrameBindings& bindings) const -> Result
{
  static_cast<void>(renderer_);
  const auto output = bloom_chain_->ResolveOutput(bindings);
  return {
    .requested = config.enable_bloom,
    .executed = config.enable_bloom && output.ready,
    .bloom_texture_srv = output.bloom_texture_srv,
  };
}

} // namespace oxygen::vortex::postprocess
