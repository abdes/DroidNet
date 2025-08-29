//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>

namespace oxygen::graphics::headless::internal {

class EngineShaders final : public Component {
  OXYGEN_COMPONENT(EngineShaders)

public:
  EngineShaders();
  ~EngineShaders() override = default;

  EngineShaders(const EngineShaders&) = delete;
  auto operator=(const EngineShaders&) -> EngineShaders& = delete;

  [[nodiscard]] auto GetShader(std::string_view id) const
    -> std::shared_ptr<IShaderByteCode>;

private:
  mutable std::unordered_map<std::string, std::shared_ptr<IShaderByteCode>>
    cache_;
};

} // namespace oxygen::graphics::headless::internal
