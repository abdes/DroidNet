//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::data {

//! Index of a level of detail within a geometry asset.
/*!
 Distinct from SubmeshIndex so that a level of detail and a submesh cannot be
 passed in each other's place. The underlying width matches the cooked
 geometry descriptors and the GPU draw records.
*/
using LodIndex
  = oxygen::NamedType<std::uint32_t, struct LodIndexTag, oxygen::Comparable,
    oxygen::Hashable, oxygen::Printable, oxygen::DefaultInitialized>;

//! Index of a submesh within the mesh of one level of detail.
using SubmeshIndex
  = oxygen::NamedType<std::uint32_t, struct SubmeshIndexTag, oxygen::Comparable,
    oxygen::Hashable, oxygen::Printable, oxygen::DefaultInitialized>;

//! Index of a mesh view within its submesh: one draw and one culling unit.
using MeshViewIndex = oxygen::NamedType<std::uint32_t, struct MeshViewIndexTag,
  oxygen::Comparable, oxygen::Hashable, oxygen::Printable,
  oxygen::DefaultInitialized>;

} // namespace oxygen::data
