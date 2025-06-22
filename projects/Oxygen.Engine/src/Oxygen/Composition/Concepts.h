//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <Oxygen/Base/Resource.h>

namespace oxygen {

//=== PooledComponent Concept ===---------------------------------------------//

//! Trait to detect if a type is a specialization of Resource<...>
template <typename T> struct is_resource : std::false_type { };

template <typename ResourceT, typename ResourceTypeList, typename HandleT>
struct is_resource<Resource<ResourceT, ResourceTypeList, HandleT>>
  : std::true_type { };

template <typename ResourceT, typename ResourceTypeList>
struct is_resource<Resource<ResourceT, ResourceTypeList>> : std::true_type { };

//! Concept to determine if a component is pooled (inherits Resource<...>)
/*!
 Checks if T is derived from any Resource<...> specialization.
 Usage: if constexpr (PooledComponent<T>) { ... }
 @tparam T Component type
 @return true if T is a pooled component, false otherwise
*/
template <typename T>
concept PooledComponent
  = is_resource<std::remove_cv_t<std::remove_reference_t<T>>>::value;

} // namespace oxygen
