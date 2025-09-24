//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ComponentPoolRegistry.h>
#include <Oxygen/Composition/Internal/GetTrulySingleInstance.h>

namespace oxygen {

auto ComponentPoolRegistry::Get() -> ComponentPoolRegistry&
{
  return composition::detail::GetTrulySingleInstance<ComponentPoolRegistry>(
    "ComponentPoolRegistry");
}

} // namespace oxygen
