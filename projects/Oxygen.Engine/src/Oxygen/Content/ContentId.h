//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::content {

//! Loader-lifetime identifier of one interned content identity.
/*!
 Minted by the loader's identity registry; it is the key under which the
 content cache stores a publication. Strongly typed so that it cannot be
 confused with a TypeId or any other 64-bit value.
*/
using ContentId = NamedType<uint64_t, struct ContentIdTag, DefaultInitialized,
  Comparable, Hashable, Printable>;

} // namespace oxygen::content
