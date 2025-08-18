//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <type_traits>

#include "Generated.Constants.h"
#include "Generated.Heaps.D3D12.h"
#include "Generated.Meta.h"
#include "Generated.RootSignature.h"

// Minimal compile-time sanity checks for the generated root-signature header.
// This file is intentionally tiny and only compiled (not linked) to ensure the
// generated header is syntactically and semantically valid with the current
// engine headers.

using namespace oxygen::engine::binding;

static_assert(kRootParamTableCount
    == static_cast<uint32_t>(std::tuple_size<decltype(kRootParamTable)>::value),
  "kRootParamTableCount must match kRootParamTable.size()");

namespace bindless_codegen_compile_check {
// Reference the table to ensure it's accessible in a translation unit.
[[maybe_unused]] static constexpr auto const& g_table = kRootParamTable;
}

// Touch a few other generated symbols to ensure those headers compile and
// symbols are visible.
static_assert(oxygen::engine::binding::kGlobalSrvCapacity > 0u,
  "Global SRV capacity must be > 0");
[[maybe_unused]] static constexpr const char* g_heap_json
  = oxygen::engine::binding::kD3D12HeapStrategyJson;
[[maybe_unused]] static constexpr const char* g_meta_tool
  = oxygen::engine::binding::kBindlessToolVersion;
