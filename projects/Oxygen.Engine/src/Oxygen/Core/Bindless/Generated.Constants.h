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

// Invalid sentinel
static constexpr uint32_t kInvalidBindlessIndex = 0xffffffffu;

// Scene constants CBV (b1), heap index 0; holds bindless indices table

static constexpr uint32_t kSceneDomainBase = 0u;
static constexpr uint32_t kSceneCapacity = 1u;

// Unified SRV table base
static constexpr uint32_t kGlobalSrvDomainBase = 1u;
static constexpr uint32_t kGlobalSrvCapacity = 2048u;

static constexpr uint32_t kMaterialsDomainBase = 2049u;
static constexpr uint32_t kMaterialsCapacity = 3047u;

static constexpr uint32_t kTexturesDomainBase = 5096u;
static constexpr uint32_t kTexturesCapacity = 65536u;

static constexpr uint32_t kSamplersDomainBase = 0u;
static constexpr uint32_t kSamplersCapacity = 256u;


} // namespace oxygen::engine::binding
