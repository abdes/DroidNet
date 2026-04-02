# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""String templates used by the bindless code generator."""

TEMPLATE_ABI_CPP = """//===----------------------------------------------------------------------===//
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

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::bindless::generated {{

namespace detail {{
using DomainTokenBase = NamedType<uint16_t, struct DomainTokenTag,
  Comparable,
  Hashable>;
using IndexSpaceTokenBase = NamedType<uint16_t, struct IndexSpaceTokenTag,
  Comparable,
  Hashable>;
}} // namespace detail

struct DomainToken : detail::DomainTokenBase {{
  using Base = detail::DomainTokenBase;
  using Base::Base;

  static constexpr uint16_t kInvalidValue = 0xFFFFu;

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {{
    return get() != kInvalidValue;
  }}
}};

struct IndexSpaceToken : detail::IndexSpaceTokenBase {{
  using Base = detail::IndexSpaceTokenBase;
  using Base::Base;

  static constexpr uint16_t kInvalidValue = 0xFFFFu;

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {{
    return get() != kInvalidValue;
  }}
}};

static_assert(sizeof(DomainToken) == sizeof(uint16_t));
static_assert(alignof(DomainToken) == alignof(uint16_t));
static_assert(sizeof(IndexSpaceToken) == sizeof(uint16_t));
static_assert(alignof(IndexSpaceToken) == alignof(uint16_t));

inline constexpr uint32_t kInvalidBindlessIndex = {invalid:#010X}U;
inline constexpr DomainToken kInvalidDomainToken {{ DomainToken::kInvalidValue }};
inline constexpr IndexSpaceToken kInvalidIndexSpaceToken {{
  IndexSpaceToken::kInvalidValue
}};

struct IndexSpaceDesc {{
  IndexSpaceToken token;
  const char* id;
}};

struct DomainDesc {{
  DomainToken token;
  const char* id;
  const char* name;
  IndexSpaceToken index_space;
  uint32_t shader_index_base;
  uint32_t capacity;
  const char* shader_access_class;
}};

{index_space_constants}

inline constexpr std::array<IndexSpaceDesc, {index_space_count}> kIndexSpaceTable = {{{{
{index_space_entries}
}}}};

{domain_constants}

inline constexpr std::array<DomainDesc, {domain_count}> kDomainTable = {{{{
{domain_entries}
}}}};

inline constexpr auto kIndexSpaceCount
  = static_cast<uint32_t>(kIndexSpaceTable.size());
inline constexpr auto kDomainCount
  = static_cast<uint32_t>(kDomainTable.size());

[[nodiscard]] constexpr auto TryGetIndexSpaceDesc(
  IndexSpaceToken token) noexcept -> const IndexSpaceDesc*
{{
  return token.IsValid() && token.get() < kIndexSpaceTable.size()
    ? &kIndexSpaceTable[token.get()]
    : nullptr;
}}

[[nodiscard]] constexpr auto TryGetDomainDesc(
  DomainToken token) noexcept -> const DomainDesc*
{{
  return token.IsValid() && token.get() < kDomainTable.size()
    ? &kDomainTable[token.get()]
    : nullptr;
}}

[[nodiscard]] constexpr auto IsShaderVisibleIndexInDomain(
  DomainToken token, uint32_t shader_index) noexcept -> bool
{{
  const auto* domain = TryGetDomainDesc(token);
  if (domain == nullptr) {{
    return false;
  }}

  return shader_index >= domain->shader_index_base
    && shader_index < (domain->shader_index_base + domain->capacity);
}}

[[nodiscard]] constexpr auto TryGetDomainByShaderVisibleIndex(
  IndexSpaceToken index_space, uint32_t shader_index) noexcept
  -> const DomainDesc*
{{
  for (const auto& domain : kDomainTable) {{
    if (domain.index_space == index_space
      && shader_index >= domain.shader_index_base
      && shader_index < (domain.shader_index_base + domain.capacity)) {{
      return &domain;
    }}
  }}
  return nullptr;
}}

}} // namespace oxygen::bindless::generated
"""

TEMPLATE_ABI_HLSL = """//===----------------------------------------------------------------------===//
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

#ifndef OXYGEN_BINDLESS_ABI_HLSL
#define OXYGEN_BINDLESS_ABI_HLSL

static const uint K_INVALID_BINDLESS_INDEX = {invalid:#010X};

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

#endif // OXYGEN_BINDLESS_ABI_HLSL
"""

TEMPLATE_RS_D3D12_CPP = """//===----------------------------------------------------------------------===//
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace oxygen::bindless::generated::d3d12 {{

enum class RootParam : uint32_t {{
{root_param_enums}
}};

{root_constants_counts}

{register_space_consts}

{root_param_structs}

{root_param_table}

}} // namespace oxygen::bindless::generated::d3d12
// clang-format on
"""

TEMPLATE_STRATEGY_D3D12_CPP = """//===----------------------------------------------------------------------===//
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

namespace oxygen::bindless::generated::d3d12 {{

static constexpr const char kStrategyJson[] = R"OXJ(
{json_body}
)OXJ";

}} // namespace oxygen::bindless::generated::d3d12
// clang-format on
"""

TEMPLATE_PIPELINE_LAYOUT_VULKAN_CPP = """//===----------------------------------------------------------------------===//
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

#include <array>
#include <cstdint>

namespace oxygen::bindless::generated::vulkan {{

enum class DescriptorSet : uint32_t {{
{descriptor_set_enums}
}};

enum class Binding : uint32_t {{
{binding_enums}
}};

enum class LayoutEntry : uint32_t {{
{layout_entry_enums}
}};

enum class LayoutEntryKind : uint8_t {{
  DescriptorSet = 0,
  PushConstants = 1,
}};

enum class DescriptorType : uint8_t {{
{descriptor_type_enums}
}};

struct DescriptorSetDesc {{
  DescriptorSet token;
  const char* id;
  uint32_t set;
}};

struct BindingDesc {{
  Binding token;
  const char* id;
  DescriptorSet descriptor_set;
  uint32_t binding;
  DescriptorType descriptor_type;
  uint32_t descriptor_count;
  bool variable_count;
  bool update_after_bind;
}};

struct DomainBindingDesc {{
  const char* domain_id;
  Binding binding;
  uint32_t array_element_base;
  uint32_t capacity;
}};

struct PipelineLayoutEntryDesc {{
  LayoutEntry token;
  const char* id;
  LayoutEntryKind kind;
  uint32_t descriptor_set_index;
  uint32_t size_bytes;
  const char* stages;
}};

inline constexpr std::array<DescriptorSetDesc, {descriptor_set_count}>
  kDescriptorSetTable = {{{{
{descriptor_set_entries}
}}}};

inline constexpr std::array<BindingDesc, {binding_count}> kBindingTable = {{{{
{binding_entries}
}}}};

inline constexpr std::array<DomainBindingDesc, {domain_binding_count}>
  kDomainBindingTable = {{{{
{domain_binding_entries}
}}}};

inline constexpr std::array<PipelineLayoutEntryDesc, {layout_entry_count}>
  kPipelineLayoutTable = {{{{
{layout_entry_entries}
}}}};

}} // namespace oxygen::bindless::generated::vulkan
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

namespace oxygen::bindless::generated {{

static constexpr const char kBindlessSourcePath[] = "{src}";
static constexpr const char kBindlessSourceVersion[] = "{src_ver}";
static constexpr const char kBindlessSchemaVersion[] = "{schema_ver}";
static constexpr const char kBindlessToolVersion[] = "{tool_ver}";
static constexpr const char kBindlessGeneratedAt[] = "{ts}";

}} // namespace oxygen::bindless::generated
// clang-format on
"""
