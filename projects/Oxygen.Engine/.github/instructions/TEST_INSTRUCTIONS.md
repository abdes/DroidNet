# Test Implementation Guidelines

## Structure
- Group tests by functionality with clear comment headers
- Use anonymous namespace to avoid symbol clashes
- Follow AAA pattern (Arrange-Act-Assert) with empty lines between phases
- Use NOLINT_* macros from GTest.h to disable linter warnings

## Fixtures
- Derive from ::testing::Test for shared setup/teardown
- Create multiple fixtures for different test types (basic, error, death)
- Implement helper methods for common actions
- Use SetUp() and TearDown() for resource management

## Test Cases
- One behavior per test with descriptive names: TestCase_WhatIsTested
- Cover normal, boundary, error, and cross-object scenarios
- Merge related positive/negative cases into comprehensive tests
- Use EXPECT_DEATH for assertion/death scenarios

## Assertions
- Use EXPECT_* and ASSERT_* with custom failure messages
- Check both state and side effects
- Always use Google Test collection matchers:
  ```cpp
  using ::testing::AllOf;
  using ::testing::Contains;
  using ::testing::IsSupersetOf;
  using ::testing::SizeIs;

  EXPECT_THAT(collection, AllOf(
    SizeIs(expected_count),
    IsSupersetOf({"item1", "item2"})
  ));
  ```

## Never Do
- Manual collection verification with loops and boolean flags
- Multiple separate EXPECT_THAT calls for same collection
- Manual size comparisons with EXPECT_EQ(container.size(), expected)
- Repeating ::testing:: namespace (use using declarations)
- Creating manual scene hierarchies (use JSON-based CreateXxxHierarchy methods)
- Changing implementation to fix tests without approval
- Testing assumptions instead of actual implementation behavior

## Always Do
- Add using ::testing::MatcherName declarations at test start
- Use STL algorithms (std::transform) instead of manual loops
- Follow existing patterns for hierarchy creation
- Test actual behavior (empty scope = full scene traversal)
- Verify proper state setup before testing complex scenarios
- Get approval before changing implementation code during test development

## Test Organization
- Use existing helper methods and base class patterns
- Create reusable helpers for common logic
- Manage all state within fixtures or locally
- Ensure tests are portable with no hardcoded dependencies
