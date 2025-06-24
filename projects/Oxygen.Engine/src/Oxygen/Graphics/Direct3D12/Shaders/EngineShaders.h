//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Composition/Component.h>

namespace oxygen::graphics {

class ShaderCompiler;
class ShaderManager;
class IShaderByteCode;

namespace d3d12 {

  class EngineShaders : public Component {
    OXYGEN_COMPONENT(EngineShaders)
  public:
    EngineShaders();
    ~EngineShaders() override;

    [[nodiscard]] auto GetShader(std::string_view unique_id) const
      -> std::shared_ptr<IShaderByteCode>;

  private:
    std::shared_ptr<ShaderCompiler> compiler_ {};
    std::unique_ptr<ShaderManager> shaders_ {};
  };

} // namespace d3d12

} // namespace oxygen::graphics
