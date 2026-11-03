//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>

namespace oxygen::content::internal {

//! The asset types the loader decodes and caches.
using AssetTypeList = oxygen::TypeList<
  // clang-format off
  data::MaterialAsset,
  data::GeometryAsset,
  data::SceneAsset,
  data::PhysicsSceneAsset,
  data::ScriptAsset,
  data::InputActionAsset,
  data::InputMappingContextAsset
  // clang-format on
  >;

//! Calls `fn.template operator()<T>()` for the asset type `T` whose type id
//! is `type`, else `otherwise()`.
template <typename Fn, typename Otherwise>
auto VisitAssetType(const TypeId type, const Fn& fn, const Otherwise& otherwise)
  -> decltype(otherwise())
{
  return detail::VisitTypeIn(AssetTypeList {}, type, fn, otherwise);
}

//! Calls `fn.template operator()<T>()` for the cached asset or resource type
//! `T` whose type id is `type`, else `otherwise()`.
template <typename Fn, typename Otherwise>
auto VisitContentType(const TypeId type, const Fn& fn,
  const Otherwise& otherwise) -> decltype(otherwise())
{
  return VisitAssetType(type, fn, [&] -> decltype(otherwise()) {
    return VisitResourceType(type, fn, otherwise);
  });
}

} // namespace oxygen::content::internal
