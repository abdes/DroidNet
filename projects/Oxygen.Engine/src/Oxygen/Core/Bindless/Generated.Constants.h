//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Meta/Bindless.yaml
// Source-Version: 1.0.1
// Schema-Version: 1.0.0
// Tool: BindlessCodeGen 1.2.1
// Generated: 2025-08-28 10:50:37

#pragma once

#include <cstdint>

namespace oxygen {

//! Invalid sentinel.
static constexpr uint32_t kInvalidBindlessIndex = 0XFFFFFFFFU;

namespace engine::binding {
  // Scene constants CBV

  static constexpr uint32_t kSceneDomainBase = 0U;
  static constexpr uint32_t kSceneCapacity = 1U;

  // Unified global bindless table

  static constexpr uint32_t kGlobalSrvDomainBase = 1U;
  static constexpr uint32_t kGlobalSrvCapacity = 2048U;

  // Unified bindless table domains

  static constexpr uint32_t kMaterialsDomainBase = 2049U;
  static constexpr uint32_t kMaterialsCapacity = 3047U;

  static constexpr uint32_t kTexturesDomainBase = 5096U;
  static constexpr uint32_t kTexturesCapacity = 65536U;

  static constexpr uint32_t kSamplersDomainBase = 0U;
  static constexpr uint32_t kSamplersCapacity = 256U;

} // namespace engine::binding
} // namespace oxygen
