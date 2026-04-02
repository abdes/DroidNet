//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <type_traits>

#include "Generated.BindlessAbi.h"
#include "Generated.Meta.h"
#include "Generated.PipelineLayout.Vulkan.h"
#include "Generated.RootSignature.D3D12.h"
#include "Generated.Strategy.D3D12.h"

// Minimal compile-time sanity checks for the generated root-signature header.
// This file is intentionally tiny and only compiled (not linked) to ensure the
// generated header is syntactically and semantically valid with the current
// engine headers.

namespace abi = oxygen::bindless::generated;
namespace d3d12 = oxygen::bindless::generated::d3d12;
namespace vulkan = oxygen::bindless::generated::vulkan;

static_assert(d3d12::kRootParamTableCount
    == static_cast<uint32_t>(
      std::tuple_size<decltype(d3d12::kRootParamTable)>::value),
  "kRootParamTableCount must match kRootParamTable.size()");
static_assert(
  abi::kDomainCount > 0u, "At least one bindless domain is required");
static_assert(vulkan::kPipelineLayoutTable.size() > 0u,
  "Vulkan pipeline layout metadata must be present");

namespace bindless_codegen_compile_check {
// Reference the table to ensure it's accessible in a translation unit.
[[maybe_unused]] static constexpr auto const& g_table = d3d12::kRootParamTable;
}

// Touch a few other generated symbols to ensure those headers compile and
// symbols are visible.
static_assert(abi::kGlobalSrvCapacity > 0u, "Global SRV capacity must be > 0");
[[maybe_unused]] static constexpr const char* g_heap_json
  = d3d12::kStrategyJson;
[[maybe_unused]] static constexpr const char* g_meta_tool
  = abi::kBindlessToolVersion;
