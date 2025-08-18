//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Bindless/Spec.yaml
// Source-Version: 1.0.1
// Schema-Version: 1.0.0
// Tool: BindlessCodeGen 1.2.0
// Generated: 2025-08-18 12:58:51

#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <cstddef>
#include <limits>

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
static constexpr uint32_t kDrawIndexConstantsCount = 1U;

// Register/space bindings (for validation or RS construction)
static constexpr uint32_t kSceneConstantsRegister = 1u; // 'b1'
static constexpr uint32_t kSceneConstantsSpace = 0u; // 'space0'
static constexpr uint32_t kDrawIndexRegister = 2u; // 'b2'
static constexpr uint32_t kDrawIndexSpace = 0u; // 'space0'

// Rich runtime descriptors and table

// Root parameter runtime descriptors (C++20 idiomatic)
enum class RootParamKind : uint8_t { DescriptorTable = 0, CBV = 1, RootConstants = 2 };

enum class RangeType : uint8_t { SRV = 0, Sampler = 1, UAV = 2 };

struct RootParamRange {
  RangeType range_type;
  uint32_t base_register;
  uint32_t register_space;
  uint32_t num_descriptors; // std::numeric_limits<uint32_t>::max() == unbounded
};

struct RootParamDesc {
  RootParamKind kind;
  uint32_t shader_register;
  uint32_t register_space;
  std::span<const RootParamRange> ranges; // empty span for non-tables
  uint32_t ranges_count;
  uint32_t constants_count; // for root constants
};


static constexpr std::array<RootParamRange, 1> kRootParam0Ranges = { {
    RootParamRange{ RangeType::SRV, 0U, 0U, std::numeric_limits<uint32_t>::max() },
} };

static constexpr std::array<RootParamRange, 1> kRootParam1Ranges = { {
    RootParamRange{ RangeType::Sampler, 0U, 0U, 256U },
} };

static constexpr std::array<RootParamDesc, 4> kRootParamTable = { {
  RootParamDesc{ RootParamKind::DescriptorTable, 0U, 0U, std::span<const RootParamRange>{ kRootParam0Ranges.data(), kRootParam0Ranges.size() }, static_cast<uint32_t>(kRootParam0Ranges.size()), 0U },
  RootParamDesc{ RootParamKind::DescriptorTable, 0U, 0U, std::span<const RootParamRange>{ kRootParam1Ranges.data(), kRootParam1Ranges.size() }, static_cast<uint32_t>(kRootParam1Ranges.size()), 0U },
  RootParamDesc{ RootParamKind::CBV, 1U, 0U, std::span<const RootParamRange>{}, 0U, 0U },
  RootParamDesc{ RootParamKind::RootConstants, 2U, 0U, std::span<const RootParamRange>{}, 0U, 1U },
} };

static constexpr auto kRootParamTableCount = static_cast<uint32_t>(kRootParamTable.size());

} // namespace oxygen::engine::binding
// clang-format on
