//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::content::internal {

//! Opaque, loader-owned token that identifies a mounted cooked container.
/*!
 A `SourceToken` is minted by `AssetLoader` when mounting a cooked container and
 is used as an identity-safe handle during decode.

 Properties:

 - It does not expose storage form (PAK vs loose cooked).
 - It is stable for the duration of a load operation.
 - It is resolvable by `AssetLoader` to a loader-owned source id.

 @note This is an internal implementation type. It is not part of the
       runtime-facing/public `AssetLoader` API.
 */
using SourceToken = oxygen::NamedType<uint32_t, struct SourceTokenTag,
  // clang-format off
  oxygen::Comparable,
  oxygen::Hashable,
  oxygen::Printable
  // clang-format on
  >;

//! Convert a SourceToken to string for logging.
inline auto to_string(const SourceToken& token) -> std::string
{
  const auto u_token = token.get();
  return "SourceToken{" + std::to_string(u_token) + "}";
}

} // namespace oxygen::content::internal
