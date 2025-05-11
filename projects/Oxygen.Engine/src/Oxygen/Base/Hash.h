//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

namespace oxygen {

//! Combines a hash seed with the hash of a value.
/*!
 Implements hash combination based on the Boost hash_combine function. Uses a
 good mixing function with the magic number 0x9e3779b9 (derived from the golden
 ratio) to distribute bits throughout the resulting hash.

 This function is useful for hashing multiple fields together, such as when
 implementing std::hash for composite types.

 \param seed The existing hash value to be combined with the new value's hash.
 \param v The value to be hashed and combined with the seed.
*/
template <class T>
void HashCombine(size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace oxygen
