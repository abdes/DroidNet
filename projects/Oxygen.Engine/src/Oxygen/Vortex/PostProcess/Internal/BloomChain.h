//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>

namespace oxygen::vortex::postprocess::internal {

class BloomChain {
public:
  struct Output {
    bool ready { false };
    ShaderVisibleIndex bloom_texture_srv { kInvalidShaderVisibleIndex };
  };

  [[nodiscard]] auto ResolveOutput(
    const PostProcessFrameBindings& bindings) const noexcept -> Output;
};

} // namespace oxygen::vortex::postprocess::internal
