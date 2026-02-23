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

#ifndef OXDAT_RESOURCE_INDEX_CONSTANT
#  define OXDAT_RESOURCE_INDEX_CONSTANT(name, value) \
    constexpr ResourceIndexT name { value };
#endif

OXDAT_RESOURCE_INDEX_CONSTANT(kFallbackResourceIndex, 0u)
OXDAT_RESOURCE_INDEX_CONSTANT(kNoResourceIndex, 0u)

#undef OXDAT_RESOURCE_INDEX_CONSTANT
