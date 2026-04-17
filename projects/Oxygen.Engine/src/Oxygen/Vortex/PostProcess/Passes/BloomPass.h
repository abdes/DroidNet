//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace postprocess::internal {
class BloomChain;
} // namespace postprocess::internal

namespace postprocess {

class BloomPass {
public:
  struct Result {
    bool requested { false };
    bool executed { false };
    ShaderVisibleIndex bloom_texture_srv { kInvalidShaderVisibleIndex };
  };

  OXGN_VRTX_API explicit BloomPass(Renderer& renderer);
  OXGN_VRTX_API ~BloomPass();

  BloomPass(const BloomPass&) = delete;
  auto operator=(const BloomPass&) -> BloomPass& = delete;
  BloomPass(BloomPass&&) = delete;
  auto operator=(BloomPass&&) -> BloomPass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Execute(const PostProcessConfig& config,
    const PostProcessFrameBindings& bindings) const -> Result;

private:
  Renderer& renderer_;
  std::unique_ptr<internal::BloomChain> bloom_chain_;
};

} // namespace postprocess

} // namespace oxygen::vortex
