//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/ProfileScope.h>

int main()
{
  using oxygen::profiling::CpuProfileScope;
  using oxygen::profiling::ProfileCategory;
  using oxygen::profiling::Var;
  using oxygen::profiling::Vars;

  CpuProfileScope scope(
    "Profiling.LinkCpu", ProfileCategory::kGeneral, Vars(Var("id", 1)));
  return 0;
}
