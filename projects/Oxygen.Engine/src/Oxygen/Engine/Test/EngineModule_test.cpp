//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/EngineModule.h>

namespace {

using oxygen::core::MakePhaseMask;
using oxygen::core::PhaseId;
using oxygen::engine::MakeModuleMask;
using oxygen::engine::ModulePhaseMask;

// Compile-time helper correctness for ModulePhaseMask and MakeModuleMask.
// These are module-level compile checks tied to `EngineModule` utilities.
constexpr ModulePhaseMask kTestModuleMask
  = MakeModuleMask<PhaseId::kInput, PhaseId::kGameplay>();
static_assert((kTestModuleMask & MakePhaseMask(PhaseId::kInput)) != 0,
  "MakeModuleMask should include PhaseId::kInput");
static_assert((kTestModuleMask & MakePhaseMask(PhaseId::kGameplay)) != 0,
  "MakeModuleMask should include PhaseId::kGameplay");

NOLINT_TEST(EngineModuleCompileTest, MakeModuleMask) { SUCCEED(); }

} // namespace
