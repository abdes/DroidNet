//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/TypeList.h>

// Forward declarations for resource types
namespace oxygen::data {
class BufferResource;
class TextureResource;
} // namespace oxygen

namespace oxygen::content {
// List of all valid resource types for the engine
using ResourceTypeList = oxygen::TypeList<
  // clang-format off
  data::BufferResource,
  data::TextureResource
  // clang-format on
  >;

// Concept: T must be a known resource type and have DescT
template <typename T>
concept PakResource = requires { typename T::DescT; }
  && (oxygen::IndexOf<T, ResourceTypeList>::value >= 0);

} // namespace oxygen::content
