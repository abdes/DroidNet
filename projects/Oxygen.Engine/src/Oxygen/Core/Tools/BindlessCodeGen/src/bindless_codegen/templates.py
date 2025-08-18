# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""String templates used by the bindless code generator."""

TEMPLATE_CPP = """//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: {src}
// Source-Version: {src_ver}
// Schema-Version: {schema_ver}
// Tool: BindlessCodeGen {tool_ver}
// Generated: {ts}

#pragma once

#include <cstdint>

namespace oxygen {{

//! Invalid sentinel.
static constexpr uint32_t kInvalidBindlessIndex = {invalid:#010X}U;

namespace engine::binding {{
{domain_consts}
}} // namespace engine::binding
}} // namespace oxygen
"""

TEMPLATE_HLSL = """//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: {src}
// Source-Version: {src_ver}
// Schema-Version: {schema_ver}
// Tool: BindlessCodeGen {tool_ver}
// Generated: {ts}

#ifndef OXYGEN_BINDLESS_LAYOUT_HLSL
#define OXYGEN_BINDLESS_LAYOUT_HLSL

static const uint K_INVALID_BINDLESS_INDEX = {invalid:#010X};

// Debug-friendly domain guards helpers (generated)
static inline bool BX_IsInDomain(uint idx, uint base, uint capacity)
{{
	return (idx >= base) && (idx < (base + capacity));
}}

static inline uint BX_TryUseGlobalIndexInDomain(uint idx, uint base, uint capacity)
{{
#ifdef BINDLESS_VALIDATE
	return BX_IsInDomain(idx, base, capacity) ? idx : {invalid:#010X};
#else
	return idx;
#endif
}}

{domain_defs}

#endif // OXYGEN_BINDLESS_LAYOUT_HLSL
"""

TEMPLATE_RS_CPP = """//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: {src}
// Source-Version: {src_ver}
// Schema-Version: {schema_ver}
// Tool: BindlessCodeGen {tool_ver}
// Generated: {ts}

#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <cstddef>
#include <limits>

namespace oxygen::engine::binding {{

// Root parameter indices
enum class RootParam : uint32_t {{
{root_param_enums}
}};

// Root constants counts (32-bit values)
{root_constants_counts}

// Register/space bindings (for validation or RS construction)
{register_space_consts}

// Rich runtime descriptors and table
{root_param_structs}

{root_param_table}

}} // namespace oxygen::engine::binding
// clang-format on
"""


TEMPLATE_HEAPS_D3D12_CPP = """//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: {src}
// Source-Version: {src_ver}
// Tool: BindlessCodeGen {tool_ver}
// Generated: {ts}

#pragma once

namespace oxygen::engine::binding {{

// D3D12 Heap Strategy JSON embedded for convenient include-only usage.
// Parse this at runtime with your preferred JSON library.
static constexpr const char kD3D12HeapStrategyJson[] = R"OXJ(
{json_body}
)OXJ";

}} // namespace oxygen::engine::binding
// clang-format on
"""

TEMPLATE_META_CPP = """//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// clang-format off

// Generated file - do not edit.
// Source: {src}
// Source-Version: {src_ver}
// Schema-Version: {schema_ver}
// Tool: BindlessCodeGen {tool_ver}
// Generated: {ts}

#pragma once

namespace oxygen::engine::binding {{

// Compile-time meta about the bindless SSoT used for generation
static constexpr const char kBindlessSourcePath[] = "{src}";
static constexpr const char kBindlessSourceVersion[] = "{src_ver}";
static constexpr const char kBindlessSchemaVersion[] = "{schema_ver}";
static constexpr const char kBindlessToolVersion[] = "{tool_ver}";
static constexpr const char kBindlessGeneratedAt[] = "{ts}";

}} // namespace oxygen::engine::binding
// clang-format on
"""
