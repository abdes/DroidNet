//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml
// Source-Version: 2.0.0
// Schema-Version: 2.0.0
// Tool: BindlessCodeGen 1.2.2
// Generated: 2026-04-02 13:22:39

#pragma once

#include <cstdint>

namespace oxygen {

//! Invalid sentinel.
static constexpr uint32_t kInvalidBindlessIndex = 0XFFFFFFFFU;

namespace engine::binding {
  // Unified global bindless table

  static constexpr uint32_t kGlobalSrvDomainBase = 1U;
  static constexpr uint32_t kGlobalSrvCapacity = 2048U;

  // Material and metadata buffer ranges

  static constexpr uint32_t kMaterialsDomainBase = 2049U;
  static constexpr uint32_t kMaterialsCapacity = 3047U;

  // Unified texture bindless range

  static constexpr uint32_t kTexturesDomainBase = 5096U;
  static constexpr uint32_t kTexturesCapacity = 65536U;

  // Global sampler table

  static constexpr uint32_t kSamplersDomainBase = 0U;
  static constexpr uint32_t kSamplersCapacity = 256U;

} // namespace engine::binding
} // namespace oxygen
