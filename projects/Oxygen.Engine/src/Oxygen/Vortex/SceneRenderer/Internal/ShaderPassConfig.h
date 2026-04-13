//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/ShaderDebugMode.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {

struct ShaderPassConfig {
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;
  bool clear_color_target = true;
  bool auto_skip_clear_when_sky_pass_present = true;
  std::optional<graphics::Color> clear_color;
  std::string debug_name { "ShaderPass" };
  graphics::FillMode fill_mode { graphics::FillMode::kSolid };
  ShaderDebugMode debug_mode { ShaderDebugMode::kDisabled };
};

} // namespace oxygen::vortex
