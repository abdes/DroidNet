//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/TypeSystem.h>

namespace oxygen::composition::testing {

class TypeSystemInitTest : public ::testing::Test {
public:
  TypeSystemInitTest()
    : registry_(oxygen::TypeRegistry::Get())
  {
  }

protected:
  oxygen::TypeRegistry& registry_;
};

} // namespace oxygen::composition::testing
