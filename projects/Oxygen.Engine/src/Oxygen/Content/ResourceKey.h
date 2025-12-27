//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content {

//! Unique identifier for a cached resource.
/*!
 Uniquely identifies a resource in the content cache. Used to retrieve or
 release resources, and can be easily constructed from a PAKFile, the resource
 type, and its index in the corresponding resource table within the PAK file.

 @see AssetLoader::MakeResourceKey
*/
using ResourceKey = oxygen::NamedType<uint64_t, struct ResourceKeyTag,
  // clang-format off
	oxygen::DefaultInitialized,
	oxygen::Comparable,
	oxygen::Hashable,
	oxygen::Printable
  // clang-format on
  >;

//! Convert a ResourceKey to string for logging.
inline auto to_string(const ResourceKey& key) -> std::string
{
  const auto u_key = key.get();
  return "ResourceKey{" + std::to_string(u_key) + "}";
}

} // namespace oxygen::content
