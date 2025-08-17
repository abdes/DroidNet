//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Bindless/Spec.yaml
// Source-Version: 1.0.0
// Schema-Version: 1.0.0
// Tool: BindlessCodeGen 1.0.0
// Generated: 2025-08-17 22:53:25

#pragma once

#include <cstdint>

namespace oxygen::engine::binding {

// Root parameter indices
enum class RootParam : uint32_t {
  kBindlessSrvTable = 0,
  kSamplerTable = 1,
  kSceneConstants = 2,
  kDrawIndex = 3,
  kCount = 4,
};

// Root constants counts (32-bit values)
static constexpr uint32_t kDrawIndexConstantsCount = 1u;

// Register/space bindings (for validation or RS construction)
static constexpr uint32_t kSceneConstantsRegister = 1u; // 'b1'
static constexpr uint32_t kSceneConstantsSpace = 0u; // 'space0'
static constexpr uint32_t kDrawIndexRegister = 2u; // 'b2'
static constexpr uint32_t kDrawIndexSpace = 0u; // 'space0'

} // namespace oxygen::engine::binding
