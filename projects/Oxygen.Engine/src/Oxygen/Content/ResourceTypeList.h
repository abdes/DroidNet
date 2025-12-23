//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>

#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/Typed.h>

// Forward declarations for resource types
namespace oxygen::data {
class BufferResource;
class TextureResource;
} // namespace oxygen::data

namespace oxygen::content {

// List of all valid resource types for the engine
using ResourceTypeList = oxygen::TypeList<
  // clang-format off
  data::BufferResource,
  data::TextureResource
  // clang-format on
  >;

static_assert(TypeListSize<ResourceTypeList>::value
    <= (std::numeric_limits<std::uint16_t>::max)(),
  "ResourceTypeList size must fit in uint16_t for type index encoding");

// Concept: T must be a known resource type and have DescT
template <typename T>
concept PakResource = requires { typename T::DescT; }
  && IsTyped<T> && (oxygen::IndexOf<T, ResourceTypeList>::value >= 0);

} // namespace oxygen::content
