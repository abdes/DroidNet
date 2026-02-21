//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <variant>

#include <Oxygen/Core/Constants.h>

namespace oxygen::data {
class GeometryAsset;
} // namespace oxygen::data

namespace oxygen::physics {

struct SphereShape final {
  float radius { 0.5F };
};

struct BoxShape final {
  Vec3 extents { 0.5F, 0.5F, 0.5F };
};

struct CapsuleShape final {
  float radius { 0.5F };
  float half_height { 0.5F };
};

struct MeshShape final {
  std::shared_ptr<const data::GeometryAsset> geometry {};
};

using CollisionShape
  = std::variant<SphereShape, BoxShape, CapsuleShape, MeshShape>;

} // namespace oxygen::physics
