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
// Generated: 2026-10-13 18:09:26

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace oxygen::bindless::generated::d3d12 {

enum class RootParam : uint32_t {
  kBindlessSrvTable = 0,
  kSamplerTable = 1,
  kViewConstants = 2,
  kRootConstants = 3,
  kCount = 4,
};

static constexpr uint32_t kRootConstantsConstantsCount = 2U;

static constexpr uint32_t kBindlessSrvTableRegister = 0U; // 't0'
static constexpr uint32_t kBindlessSrvTableSpace = 0U; // 'space0'
static constexpr uint32_t kSamplerTableRegister = 0U; // 's0'
static constexpr uint32_t kSamplerTableSpace = 0U; // 'space0'
static constexpr uint32_t kViewConstantsRegister = 1U; // 'b1'
static constexpr uint32_t kViewConstantsSpace = 0U; // 'space0'
static constexpr uint32_t kRootConstantsRegister = 2U; // 'b2'
static constexpr uint32_t kRootConstantsSpace = 0U; // 'space0'

// Root parameter runtime descriptors (C++20 idiomatic)
enum class RootParamKind : uint8_t {
  DescriptorTable = 0,
  CBV = 1,
  RootConstants = 2,
};

enum class RangeType : uint8_t { SRV = 0, Sampler = 1, UAV = 2, CBV = 3 };

struct RootParamRange {
  RangeType range_type;
  uint32_t base_register;
  uint32_t register_space;
  uint32_t num_descriptors; // std::numeric_limits<uint32_t>::max() == unbounded
};

struct RootParamDesc {
  RootParamKind kind { RootParamKind::DescriptorTable };
  uint32_t shader_register { 0U };
  uint32_t register_space { 0U };
  std::span<const RootParamRange> ranges; // empty span for non-tables
  uint32_t ranges_count { 0U };
  uint32_t constants_count { 0U }; // for root constants
};

static constexpr std::array<RootParamRange, 1> kRootParam0Ranges = {
  {
    RootParamRange {
      .range_type = RangeType::SRV,
      .base_register = 0U,
      .register_space = 0U,
      .num_descriptors = std::numeric_limits<uint32_t>::max(),
    },
  },
};

static constexpr std::array<RootParamRange, 1> kRootParam1Ranges = {
  {
    RootParamRange {
      .range_type = RangeType::Sampler,
      .base_register = 0U,
      .register_space = 0U,
      .num_descriptors = 256U,
    },
  },
};

static constexpr std::array<RootParamDesc, 4> kRootParamTable = { {
  RootParamDesc { .kind = RootParamKind::DescriptorTable,
    .shader_register = 0U,
    .register_space = 0U,
    .ranges = std::span<const RootParamRange> { kRootParam0Ranges.data(),
      kRootParam0Ranges.size(), },
    .ranges_count = static_cast<uint32_t>(kRootParam0Ranges.size()),
    .constants_count = 0U, },
  RootParamDesc { .kind = RootParamKind::DescriptorTable,
    .shader_register = 0U,
    .register_space = 0U,
    .ranges = std::span<const RootParamRange> { kRootParam1Ranges.data(),
      kRootParam1Ranges.size(), },
    .ranges_count = static_cast<uint32_t>(kRootParam1Ranges.size()),
    .constants_count = 0U, },
  RootParamDesc { .kind = RootParamKind::CBV,
    .shader_register = 1U,
    .register_space = 0U,
    .ranges = std::span<const RootParamRange> {},
    .ranges_count = 0U,
    .constants_count = 0U, },
  RootParamDesc { .kind = RootParamKind::RootConstants,
    .shader_register = 2U,
    .register_space = 0U,
    .ranges = std::span<const RootParamRange> {},
    .ranges_count = 0U,
    .constants_count = 2U, },
}, };

static constexpr auto kRootParamTableCount
  = static_cast<uint32_t>(kRootParamTable.size());

} // namespace oxygen::bindless::generated::d3d12
