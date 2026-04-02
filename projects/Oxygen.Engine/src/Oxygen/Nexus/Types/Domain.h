//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Nexus/api_export.h>

namespace oxygen::nexus {

//! Semantic bindless domain key.
/*!
 Domain identity is now a thin wrapper around the generated `DomainToken`.
 Higher-level systems keep using `DomainKey` as their naming surface, but the
 underlying semantic selector is the generated token rather than
 `(ResourceViewType, DescriptorVisibility)`.
*/
struct DomainKey {
  oxygen::bindless::DomainToken domain {};

  bool operator==(DomainKey const& o) const noexcept
  {
    return domain == o.domain;
  }
};

struct DomainRange {
  oxygen::bindless::ShaderVisibleIndex start {};
  oxygen::bindless::Capacity capacity {};
};

struct DomainKeyHash {
  std::size_t operator()(DomainKey const& k) const noexcept
  {
    return static_cast<std::size_t>(k.domain.get());
  }
};

} // namespace oxygen::nexus
