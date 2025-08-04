//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::content {

//! Unique identifier for a cached resource.
/*!
 Uniquely identifies a resource in the content cache. Used to retrieve or
 release resources, and can be easily constructed from a PAKFile, the resource
 type, and its index in the corresponding resource table within the PAK file.

 @see AssetLoader::MakeResourceKey
*/
using ResourceKey = uint64_t;

} // namespace oxygen::content
