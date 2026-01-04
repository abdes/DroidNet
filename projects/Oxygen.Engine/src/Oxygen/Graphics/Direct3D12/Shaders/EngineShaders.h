//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::graphics {

class ShaderManager;
class IShaderByteCode;

namespace d3d12 {

  class EngineShaders : public Component {
    OXYGEN_COMPONENT(EngineShaders)
  public:
    explicit EngineShaders(oxygen::PathFinderConfig path_finder_config);
    ~EngineShaders() override;

    [[nodiscard]] auto GetShader(const ShaderRequest& request) const
      -> std::shared_ptr<IShaderByteCode>;

  private:
    oxygen::PathFinderConfig path_finder_config_;
    std::unique_ptr<ShaderManager> shaders_ {};
  };

} // namespace d3d12

} // namespace oxygen::graphics
