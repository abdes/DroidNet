//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content {

//! A collision shape descriptor as the loader read and checked it: the fixed
//! record and, for a compound shape, its child records in cooked order.
struct CollisionShapeData final {
  data::pak::physics::CollisionShapeAssetDesc descriptor {};
  //! Empty for any shape but a compound, which has at least one.
  std::vector<data::pak::physics::CompoundShapeChildDesc> children;
};

} // namespace oxygen::content
