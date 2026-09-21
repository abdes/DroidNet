//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <tuple>

#include <Oxygen/Vortex/PostProcess/Internal/BloomChain.h>
#include <Oxygen/Vortex/PostProcess/Passes/BloomPass.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>

namespace oxygen::vortex::postprocess {

BloomPass::BloomPass(Renderer& renderer)
  : renderer_(renderer)
  , bloom_chain_(std::make_unique<internal::BloomChain>())
{
}

BloomPass::~BloomPass() = default;

auto BloomPass::Execute(const ResolvedPostProcessConfig& config,
  const PostProcessFrameBindings& bindings) const -> Result
{
  std::ignore = renderer_;
  if (!config.Settings().enable_bloom) {
    return {};
  }
  const auto output = bloom_chain_->ResolveOutput(bindings);
  return {
    .requested = true,
    .executed = output.ready,
    .bloom_texture_srv = output.bloom_texture_srv,
  };
}

} // namespace oxygen::vortex::postprocess
