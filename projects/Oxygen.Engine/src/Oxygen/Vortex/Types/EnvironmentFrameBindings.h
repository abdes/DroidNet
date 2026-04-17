//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>

namespace oxygen::vortex {

//! Bindless environment-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) EnvironmentFrameBindings {
  ShaderVisibleIndex environment_static_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex environment_view_slot { kInvalidShaderVisibleIndex };
  EnvironmentProbeBindings probes {};
  EnvironmentEvaluationParameters evaluation {};
  EnvironmentAmbientBridgeBindings ambient_bridge {};
};

static_assert(sizeof(EnvironmentFrameBindings) == 64);
static_assert(
  alignof(EnvironmentFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(EnvironmentFrameBindings) % 16 == 0);

} // namespace oxygen::vortex
