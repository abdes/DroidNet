//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>

int main()
{
  using oxygen::profiling::Var;
  using oxygen::profiling::Vars;
  [[maybe_unused]] const auto vars = Vars(Var("id", 1));
  return 0;
}
