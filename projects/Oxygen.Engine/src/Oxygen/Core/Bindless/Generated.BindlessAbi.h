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
// Generated: 2026-04-02 21:33:20

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::bindless::generated {

namespace detail {
using DomainTokenBase = NamedType<uint16_t, struct DomainTokenTag,
  Comparable,
  Hashable>;
using IndexSpaceTokenBase = NamedType<uint16_t, struct IndexSpaceTokenTag,
  Comparable,
  Hashable>;
} // namespace detail

struct DomainToken : detail::DomainTokenBase {
  using Base = detail::DomainTokenBase;
  using Base::Base;

  static constexpr uint16_t kInvalidValue = 0xFFFFu;

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {
    return get() != kInvalidValue;
  }
};

struct IndexSpaceToken : detail::IndexSpaceTokenBase {
  using Base = detail::IndexSpaceTokenBase;
  using Base::Base;

  static constexpr uint16_t kInvalidValue = 0xFFFFu;

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {
    return get() != kInvalidValue;
  }
};

static_assert(sizeof(DomainToken) == sizeof(uint16_t));
static_assert(alignof(DomainToken) == alignof(uint16_t));
static_assert(sizeof(IndexSpaceToken) == sizeof(uint16_t));
static_assert(alignof(IndexSpaceToken) == alignof(uint16_t));

inline constexpr uint32_t kInvalidBindlessIndex = 0XFFFFFFFFU;
inline constexpr DomainToken kInvalidDomainToken { DomainToken::kInvalidValue };
inline constexpr IndexSpaceToken kInvalidIndexSpaceToken {
  IndexSpaceToken::kInvalidValue
};

struct IndexSpaceDesc {
  IndexSpaceToken token;
  const char* id;
};

struct DomainDesc {
  DomainToken token;
  const char* id;
  const char* name;
  IndexSpaceToken index_space;
  uint32_t shader_index_base;
  uint32_t capacity;
  const char* shader_access_class;
};

inline constexpr IndexSpaceToken kSrvUavCbvIndexSpace { 0U };
inline constexpr IndexSpaceToken kSamplerIndexSpace { 1U };

inline constexpr std::array<IndexSpaceDesc, 2> kIndexSpaceTable = {{
  IndexSpaceDesc{ kSrvUavCbvIndexSpace, "srv_uav_cbv" },
  IndexSpaceDesc{ kSamplerIndexSpace, "sampler" },
}};

// Unified global bindless table sized for production large geometry-heavy scenes
inline constexpr DomainToken kGlobalSrvDomain { 0U };
inline constexpr uint32_t kGlobalSrvShaderIndexBase = 1U;
inline constexpr uint32_t kGlobalSrvCapacity = 32768U;

// Material and metadata buffer ranges
inline constexpr DomainToken kMaterialsDomain { 1U };
inline constexpr uint32_t kMaterialsShaderIndexBase = 32769U;
inline constexpr uint32_t kMaterialsCapacity = 3047U;

// Unified texture bindless range
inline constexpr DomainToken kTexturesDomain { 2U };
inline constexpr uint32_t kTexturesShaderIndexBase = 35816U;
inline constexpr uint32_t kTexturesCapacity = 65536U;

// Global sampler table
inline constexpr DomainToken kSamplersDomain { 3U };
inline constexpr uint32_t kSamplersShaderIndexBase = 0U;
inline constexpr uint32_t kSamplersCapacity = 256U;

inline constexpr std::array<DomainDesc, 4> kDomainTable = {{
  DomainDesc{ kGlobalSrvDomain, "srv_global", "GlobalSRV", kSrvUavCbvIndexSpace, kGlobalSrvShaderIndexBase, kGlobalSrvCapacity, "buffer_srv" },
  DomainDesc{ kMaterialsDomain, "materials", "Materials", kSrvUavCbvIndexSpace, kMaterialsShaderIndexBase, kMaterialsCapacity, "buffer_srv" },
  DomainDesc{ kTexturesDomain, "textures", "Textures", kSrvUavCbvIndexSpace, kTexturesShaderIndexBase, kTexturesCapacity, "texture_srv" },
  DomainDesc{ kSamplersDomain, "samplers", "Samplers", kSamplerIndexSpace, kSamplersShaderIndexBase, kSamplersCapacity, "sampler" },
}};

inline constexpr auto kIndexSpaceCount
  = static_cast<uint32_t>(kIndexSpaceTable.size());
inline constexpr auto kDomainCount
  = static_cast<uint32_t>(kDomainTable.size());

[[nodiscard]] constexpr auto TryGetIndexSpaceDesc(
  IndexSpaceToken token) noexcept -> const IndexSpaceDesc*
{
  return token.IsValid() && token.get() < kIndexSpaceTable.size()
    ? &kIndexSpaceTable[token.get()]
    : nullptr;
}

[[nodiscard]] constexpr auto TryGetDomainDesc(
  DomainToken token) noexcept -> const DomainDesc*
{
  return token.IsValid() && token.get() < kDomainTable.size()
    ? &kDomainTable[token.get()]
    : nullptr;
}

[[nodiscard]] constexpr auto IsShaderVisibleIndexInDomain(
  DomainToken token, uint32_t shader_index) noexcept -> bool
{
  const auto* domain = TryGetDomainDesc(token);
  if (domain == nullptr) {
    return false;
  }

  return shader_index >= domain->shader_index_base
    && shader_index < (domain->shader_index_base + domain->capacity);
}

[[nodiscard]] constexpr auto TryGetDomainByShaderVisibleIndex(
  IndexSpaceToken index_space, uint32_t shader_index) noexcept
  -> const DomainDesc*
{
  for (const auto& domain : kDomainTable) {
    if (domain.index_space == index_space
      && shader_index >= domain.shader_index_base
      && shader_index < (domain.shader_index_base + domain.capacity)) {
      return &domain;
    }
  }
  return nullptr;
}

} // namespace oxygen::bindless::generated
