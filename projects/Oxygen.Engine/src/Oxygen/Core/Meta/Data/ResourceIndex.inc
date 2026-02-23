//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared resource index type and constants.
//
// This catalog is normative for the resource-index sentinel contract.
// `kNoResourceIndex` is assumed to be zero across Pak/runtime integration.

using ResourceIndexT = NamedType<uint32_t, struct ResourceIndexTag,
  // clang-format off
  DefaultInitialized,
  ImplicitlyConvertibleTo<uint32_t>::templ,
  Comparable,
  Hashable,
  Printable>; // clang-format on

static_assert(sizeof(ResourceIndexT) == sizeof(uint32_t),
  "ResourceIndex contract violated: ResourceIndexT size must be 4 bytes");

#ifndef OXDAT_RESOURCE_INDEX_CONSTANT
#  define OXDAT_RESOURCE_INDEX_CONSTANT(name, value) \
    constexpr ResourceIndexT name { value };
#endif

OXDAT_RESOURCE_INDEX_CONSTANT(kFallbackResourceIndex, 0u)
OXDAT_RESOURCE_INDEX_CONSTANT(kNoResourceIndex, 0u)

#undef OXDAT_RESOURCE_INDEX_CONSTANT
