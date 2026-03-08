//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>

namespace oxygen::engine {

enum class ShadowDomain : std::uint32_t {
  kDirectional = 0U,
  kSpot = 1U,
  kPoint = 2U,
};

enum class ShadowImplementationKind : std::uint32_t {
  kNone = 0U,
  kConventional = 1U,
  kVirtual = 2U,
};

enum class ShadowProductFlags : std::uint32_t {
  kNone = 0U,
  kValid = OXYGEN_FLAG(0),
  kContactShadows = OXYGEN_FLAG(1),
  kSunLight = OXYGEN_FLAG(2),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(ShadowProductFlags)

//! Shared shadow-product metadata published by ShadowManager.
struct alignas(16) ShadowInstanceMetadata {
  std::uint32_t light_index { 0U };
  std::uint32_t payload_index { 0U };
  std::uint32_t domain { static_cast<std::uint32_t>(
    ShadowDomain::kDirectional) };
  std::uint32_t implementation_kind { static_cast<std::uint32_t>(
    ShadowImplementationKind::kNone) };

  std::uint32_t flags { 0U };
  std::uint32_t _reserved0 { 0U };
  std::uint32_t _reserved1 { 0U };
  std::uint32_t _reserved2 { 0U };
};

static_assert(sizeof(ShadowInstanceMetadata) == 32U,
  "ShadowInstanceMetadata size must match HLSL packing");
static_assert(sizeof(ShadowInstanceMetadata) % 16U == 0U,
  "ShadowInstanceMetadata size must be 16-byte aligned");

} // namespace oxygen::engine
