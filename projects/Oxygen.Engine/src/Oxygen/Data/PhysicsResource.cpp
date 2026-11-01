//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Serio/ByteBuffer.h>

namespace oxygen::data {

PhysicsResource::PhysicsResource(
  pak::physics::PhysicsResourceDesc desc, serio::ByteBuffer data)
  : desc_(desc)
  , data_(std::move(data))
{
}

} // namespace oxygen::data
