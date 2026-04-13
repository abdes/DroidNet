//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Core/Types/PostProcess.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {

//! Configuration for Vortex tone mapping and exposure planning.
struct ToneMapPassConfig {
  std::shared_ptr<graphics::Texture> source_texture;
  std::shared_ptr<graphics::Texture> output_texture;
  engine::ExposureMode exposure_mode { engine::ExposureMode::kManual };
  float manual_exposure { 1.0F };
  float gamma { 2.2F };
  engine::ToneMapper tone_mapper { engine::ToneMapper::kNone };
  std::string debug_name { "ToneMapPass" };
};

} // namespace oxygen::vortex
