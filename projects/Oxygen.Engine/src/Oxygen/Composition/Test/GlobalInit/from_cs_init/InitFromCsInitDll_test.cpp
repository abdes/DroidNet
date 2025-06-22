//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/TypeSystem.h>

#include "../TypeSystemInitTest.h"

// Force linking with the oxygen-cs-init DLL by explicitly using it so that the
// linker does not optimize it out.
namespace oxygen {
class TypeRegistry;
} // namespace oxygen
extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
namespace {
[[maybe_unused]] const auto* const ts_registry_unused
  = InitializeTypeRegistry();
} // namespace

using oxygen::composition::testing::TypeSystemInitTest;

namespace {

class MyType final { };

NOLINT_TEST_F(TypeSystemInitTest, TypeRegistryWorks)
{
  // The TypeRegistry should be initialized
  auto type_id = registry_.RegisterType("MyType");
  EXPECT_EQ(type_id, registry_.GetTypeId("MyType"));
}

} // namespace
