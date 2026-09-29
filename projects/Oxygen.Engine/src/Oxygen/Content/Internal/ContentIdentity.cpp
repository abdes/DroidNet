//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <type_traits>
#include <variant>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::content::internal {

auto ResourceTypeId(const ResourceKind kind) noexcept -> TypeId
{
  switch (kind) {
  case ResourceKind::kBuffer:
    return data::BufferResource::ClassTypeId();
  case ResourceKind::kTexture:
    return data::TextureResource::ClassTypeId();
  case ResourceKind::kScript:
    return data::ScriptResource::ClassTypeId();
  case ResourceKind::kPhysics:
    return data::PhysicsResource::ClassTypeId();
  }
  return kInvalidTypeId;
}

auto ContentIdentityHash::operator()(
  const ContentIdentity& identity) const noexcept -> size_t
{
  auto seed = identity.index();
  std::visit(
    [&seed](const auto& value) {
      using T = std::remove_cvref_t<decltype(value)>;
      if constexpr (std::is_same_v<T, AssetIdentity>) {
        HashCombine(seed, value.source);
        HashCombine(seed, value.asset);
      } else if constexpr (std::is_same_v<T, CookedResourceIdentity>) {
        HashCombine(seed, value.source);
        HashCombine(seed, value.kind);
        HashCombine(seed, value.index);
      } else {
        HashCombine(seed, value.kind);
        HashCombine(seed, value.serial);
      }
    },
    identity);
  return seed;
}

} // namespace oxygen::content::internal
