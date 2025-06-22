//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/TypeSystem.h>

#include "../TypeSystemInitTest.h"

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
