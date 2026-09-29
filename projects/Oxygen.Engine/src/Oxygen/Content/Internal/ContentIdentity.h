//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

using ContentId = NamedType<uint64_t, struct ContentIdTag, DefaultInitialized,
  Comparable, Hashable, Printable>;

enum class ResourceKind : uint8_t { kBuffer, kTexture, kScript, kPhysics };

[[nodiscard]] auto ResourceTypeId(ResourceKind kind) noexcept -> TypeId;

struct AssetIdentity final {
  data::SourceInstanceId source {};
  data::AssetKey asset {};

  auto operator==(const AssetIdentity&) const -> bool = default;
};

struct CookedResourceIdentity final {
  data::SourceInstanceId source {};
  ResourceKind kind {};
  uint32_t index = 0;

  auto operator==(const CookedResourceIdentity&) const -> bool = default;
};

struct SyntheticResourceIdentity final {
  ResourceKind kind {};
  uint64_t serial = 0;

  auto operator==(const SyntheticResourceIdentity&) const -> bool = default;
};

using ContentIdentity = std::variant<AssetIdentity, CookedResourceIdentity,
  SyntheticResourceIdentity>;

struct ContentIdentityHash final {
  auto operator()(const ContentIdentity& identity) const noexcept -> size_t;
};

} // namespace oxygen::content::internal
