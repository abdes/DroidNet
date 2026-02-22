//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/PhysicsResource.h>

namespace oxygen::data {

PhysicsResource::PhysicsResource(
  pak::PhysicsResourceDesc desc, std::vector<uint8_t> data)
  : desc_(desc)
  , data_(std::move(data))
{
}

} // namespace oxygen::data
