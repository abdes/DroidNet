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
class ScriptResource;
class PhysicsResource;
} // namespace oxygen::data

namespace oxygen::content {

// List of all valid resource types for the engine
using ResourceTypeList = oxygen::TypeList<
  // clang-format off
  data::BufferResource,
  data::TextureResource,
  data::ScriptResource,
  data::PhysicsResource
  // clang-format on
  >;

static_assert(TypeListSize<ResourceTypeList>::value <= 65535,
  "ResourceTypeList size must fit in uint16_t for type index encoding");

// Concept: T must be a known resource type and have DescT
template <typename T>
concept PakResource = requires { typename T::DescT; } && IsTyped<T>
  && (IndexOf<T, ResourceTypeList>::value
    < TypeListSize<ResourceTypeList>::value);

} // namespace oxygen::content
