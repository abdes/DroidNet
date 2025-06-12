//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

/// @file SceneTestFixtures.h
/// @brief Convenience header that includes all Scene testing fixtures.
///
/// This header provides a single include point for all Scene module test
/// fixtures, reducing code duplication and providing consistent testing
/// infrastructure across all Scene test files.
///
/// Usage:
/// @code
/// #include <Oxygen/Scene/Test/Fixtures/SceneTestFixtures.h>
///
/// class MySceneTest : public oxygen::scene::testing::SceneTest::BasicTest {
///   // Your test implementation
/// };
/// @endcode

// Core scene testing infrastructure
#include <Oxygen/Scene/Test/Fixtures/SceneTest.h>

// Specialized testing fixtures
#include <Oxygen/Scene/Test/Fixtures/SceneCloningTest.h>
#include <Oxygen/Scene/Test/Fixtures/SceneFlagsTest.h>
#include <Oxygen/Scene/Test/Fixtures/SceneTraversalTest.h>

namespace oxygen::scene::testing {

/// @brief Convenience aliases for commonly used test fixtures.
namespace fixtures {
  // Basic scene testing
  using BasicTest = SceneBasicTest;
  using ErrorTest = SceneErrorTest;
  using DeathTest = SceneDeathTest;
  using EdgeCaseTest = SceneEdgeCaseTest;
  using PerformanceTest = ScenePerformanceTest;
  using FunctionalTest = SceneFunctionalTest;

  // Traversal testing
  using BasicTraversalTest = SceneTraversalBasicTest;
  using TraversalFilterTest = SceneTraversalFilterTest;
  using TraversalVisitorTest = SceneTraversalVisitorTest;
  using TraversalPerformanceTest = SceneTraversalPerformanceTest;
  using TraversalTransformTest = SceneTraversalTransformTest;

  // Flags testing
  using BasicFlagsTest = TestSceneFlagsBasicTest;
  using FlagsInheritanceTest = TestSceneFlagsInheritanceTest;
  using FlagsErrorTest = TestSceneFlagsErrorTest;
  using FlagsEdgeCaseTest = TestSceneFlagsEdgeCaseTest;
  using AtomicFlagsTest = TestSceneFlagsAtomicTest;

  // Cloning testing
  using BasicCloningTest = SceneCloningBasicTest;
  using DeepCloningTest = SceneCloningDeepTest;
  using CloningPerformanceTest = SceneCloningPerformanceTest;
  using SerializationTest = SceneSerializationTest;
  using CrossSceneCloningTest = SceneCrossSceneCloningTest;
}

} // namespace oxygen::scene::testing
