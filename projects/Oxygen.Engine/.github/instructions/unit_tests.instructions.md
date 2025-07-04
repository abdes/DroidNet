---
applyTo: '**/*.cpp;**/*.h'
---
# UNIT TEST RULES

## TEST CASES

- Use Google Test; use Google Mock only if required.
- Include src/Oxygen/Testing/GTest.h, when not already included, instead of gtest.h and gmock.h.
- Write scenario-based tests with clear, descriptive names.
- Group tests by functionality with detailed comment headers (describe hierarchy, scenario, or feature).
- Place all tests in anonymous namespaces to avoid symbol clashes.
- Follow the AAA pattern (Arrange, Act, Assert) with clear comments and empty lines between phases.
- Use NOLINT_* macros from the file src/Oxygen/Testing/GTest.h for all test cases.
- Use GCHECK_F for assertion that use helper methods with EXPECT_ inside of them. Use TRACE_GCHECK_F only when the failure location is ambiguous, and always provide a concise tag. Never use SCOPED_TRACE directly.

## FIXTURES AND HELPERS

- Create separate fixtures for different test types (basic, error, death, edge, complex).
- Implement helper methods for common actions, node creation, and expectations.
- Create reusable helpers for logic and expectation checks (e.g., node order, presence, filtering).
- Use and extend existing helper methods and base class patterns as needed.
- Use SetUp() and TearDown() for resource management and clean state.
- Manage all state within fixtures or locally; never use global or static state.

## TEST CASE DESIGN

- Test one behavior per test; use the format TestCase_WhatIsTested (e.g SceneEdgeTest_EmptyScopeTraversesFullScene, CompositionErrorTest_GetComponent_WrongType_Throws ).
- Add doc comments (//! or /*! ...*/) above each test to describe intent and scenario. Brief comment only if the test is trivial.
- Cover normal, boundary, error, edge, and cross-object scenarios.
- Merge related positive/negative cases into comprehensive tests when possible.
- Use EXPECT_DEATH for assertion/death scenarios.
- Verify proper state setup before complex scenarios.
- Test actual implementation behavior (e.g., empty scope = full scene traversal).
- Use expressive, scenario-driven test design; avoid trivial or assumption-based tests.

## ASSERTIONS AND MATCHERS

- Use EXPECT_*and ASSERT_*, custom failure messages only when they add valuable information not shown by gtest diagnostics already.
- Check both state and side effects; verify node order, presence, and filtering
  as appropriate.
- Use Google Test collection matchers when applicable instead of hand writing the expectations: using ::testing::AllOf; using
  ::testing::Contains; using ::testing::IsSupersetOf; using ::testing::SizeIs;
  EXPECT_THAT(collection, AllOf( SizeIs(expected_count), IsSupersetOf({"item1",
  "item2"}) ));
- Add using ::testing::MatcherName declarations at test start for the matchers used in the tests.
- Use expectation helpers (e.g., ExpectVisitedNodes, ExpectContainsExactlyNodes)
  for clarity and reuse.
