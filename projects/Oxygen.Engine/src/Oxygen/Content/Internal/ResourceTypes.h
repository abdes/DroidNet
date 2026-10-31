//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::content::internal {

//! The number of resource types.
inline constexpr auto kResourceTypeCount
  = TypeListSize<ResourceTypeList>::value;

//! The index of the resource type `T` in `ResourceTypeList`.
template <typename T>
inline constexpr auto kResourceTypeIndex = IndexOf<T, ResourceTypeList>::value;

namespace detail {
  template <typename... T, typename Fn, typename Otherwise>
  auto VisitResourceTypeIn(TypeList<T...> /*types*/, const TypeId type,
    const Fn& fn, const Otherwise& otherwise) -> decltype(otherwise())
  {
    auto result = std::optional<decltype(otherwise())> {};
    static_cast<void>(((type == T::ClassTypeId()
                         && (result.emplace(fn.template operator()<T>()), true))
      || ...));
    return result ? std::move(*result) : otherwise();
  }
} // namespace detail

//! Calls `fn.template operator()<T>()` with the resource type `T` that `type`
//! names, or `otherwise()` when `type` names no resource type; both return the
//! same type.
template <typename Fn, typename Otherwise>
auto VisitResourceType(const TypeId type, const Fn& fn,
  const Otherwise& otherwise) -> decltype(otherwise())
{
  return detail::VisitResourceTypeIn(ResourceTypeList {}, type, fn, otherwise);
}

//! `VisitResourceType` for a type that must be a resource type.
/*!
 @throw std::invalid_argument When `type` names no resource type.
*/
template <typename Fn> auto WithResourceType(const TypeId type, const Fn& fn)
{
  using Result = decltype(fn.template operator()<data::BufferResource>());
  return VisitResourceType(type, fn, []() -> Result {
    throw std::invalid_argument("Unsupported resource type");
  });
}

} // namespace oxygen::content::internal
