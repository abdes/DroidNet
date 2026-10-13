//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>

#include "./BaseCompositionTest.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::TypeId;

namespace {
// Forward declare pooled type
class PooledComponent;
// TestResourceTypeList after forward declaration
using TestResourceTypeList = oxygen::TypeList<PooledComponent>;

class SimpleComponent1 final : public oxygen::Component {
  OXYGEN_COMPONENT(SimpleComponent1)
};

class SimpleComponent2 final : public oxygen::Component {
  OXYGEN_COMPONENT(SimpleComponent2)
};

class PooledComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledComponent, TestResourceTypeList)
  OXYGEN_COMPONENT_REQUIRES(SimpleComponent1)
public:
  explicit PooledComponent(int v)
    : value_(v)
  {
  }
  int value_;
};

class NonPooledComponent final : public oxygen::Component {
  OXYGEN_COMPONENT(NonPooledComponent)
  OXYGEN_COMPONENT_REQUIRES(SimpleComponent1, SimpleComponent2)
public:
  explicit NonPooledComponent(int v)
    : value_(v)
  {
  }
  int value_;
};

class TestComposition : public oxygen::Composition {
  OXYGEN_TYPED(TestComposition)
public:
  using Base = Composition;

  // Expose protected methods for testing
  using Base::AddComponent;
  using Base::RemoveComponent;
  using Base::ReplaceComponent;
};

class PrintAndDebugTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  TestComposition composition_;
};

//! PrintComponents outputs pooled and non-pooled component types and storage
//! kinds.
NOLINT_TEST_F(PrintAndDebugTest, PrintHybridComponents)
{
  // Arrange
  composition_.AddComponent<SimpleComponent1>();
  composition_.AddComponent<SimpleComponent2>();
  composition_.AddComponent<PooledComponent>(1);
  composition_.AddComponent<NonPooledComponent>(2);

  // Act
  std::ostringstream os;
  composition_.PrintComponents(os);
  const std::string output = os.str();

  // Assert
  using testing::HasSubstr;
  EXPECT_THAT(output, HasSubstr("PooledComponent"))
    << "Should print pooled component type name";
  EXPECT_THAT(output, HasSubstr("NonPooledComponent"))
    << "Should print non-pooled component type name";
  EXPECT_THAT(output, HasSubstr("Pooled")) << "Should indicate pooled storage";
  EXPECT_THAT(output, HasSubstr("Direct")) << "Should indicate direct storage";
}

NOLINT_TEST_F(PrintAndDebugTest, LogComponents_OutputsToStdoutAndStderr)
{
  loguru::g_global_verbosity = loguru::Verbosity_INFO;
  // Arrange
  composition_.AddComponent<SimpleComponent1>();
  composition_.AddComponent<SimpleComponent2>();
  composition_.AddComponent<PooledComponent>(1);
  composition_.AddComponent<NonPooledComponent>(2);

  // Act: Capture both stdout and stderr
  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();
  composition_.LogComponents();
  std::string out = testing::internal::GetCapturedStdout();
  std::string err = testing::internal::GetCapturedStderr();

  // Assert: Check for expected substrings in either output
  using testing::HasSubstr;
  std::string combined = out + err;
  EXPECT_THAT(combined, HasSubstr("Local Components"))
    << "Should log local components section";
  EXPECT_THAT(combined, HasSubstr("Pooled Components"))
    << "Should log pooled components section";
  EXPECT_THAT(combined, HasSubstr("SimpleComponent1"))
    << "Should log SimpleComponent1";
  EXPECT_THAT(combined, HasSubstr("SimpleComponent2"))
    << "Should log SimpleComponent2";
  EXPECT_THAT(combined, HasSubstr("PooledComponent"))
    << "Should log PooledComponent";
  EXPECT_THAT(combined, HasSubstr("NonPooledComponent"))
    << "Should log NonPooledComponent";
}
} // namespace
