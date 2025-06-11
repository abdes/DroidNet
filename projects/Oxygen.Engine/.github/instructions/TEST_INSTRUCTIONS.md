# Model Test Suite Guidelines

This document summarizes the key characteristics and best practices observed in the `Scene_basic_test.cpp` test suite. Follow these guidelines to ensure consistency, clarity, and thoroughness in future test suites.

---

## 1. Clear Structure and Organization

- **Logical Grouping:** Organize tests by functionality (e.g., construction, node creation, destruction, error handling, statistics, edge cases).
- **Section Headers:** Use clear comment headers to separate test categories.
- **Namespace Usage:** Wrap all tests in an anonymous namespace to avoid symbol clashes.

## 2. Fixture-Based Testing

- **Test Fixtures:** Derive fixtures from `::testing::Test` (e.g., `SceneBasicTest`, `SceneBasicErrorTest`, `SceneBasicDeathTest`) for shared setup/teardown and helpers.
- **Setup/TearDown:** Use `SetUp()` and `TearDown()` for resource management.
- Use alternate macros NOLINT_* from GTest.h include file to disable linter warnings.

## 3. Helper Methods

- **Reusable Helpers:** Implement helper methods for common actions (e.g., `CreateNode`, `DestroyNode`, `ExpectNodeValidWithName`).
- **Encapsulation:** Encapsulate repetitive logic and assertions in helpers to improve readability and maintainability.

## 4. Thorough and Focused Test Cases

- **Arrange-Act-Assert:** Follow the AAA pattern, with clear comments for each phase. Empty line before each phase.
- **Single Responsibility:** Each test should check one specific behavior or scenario.
- **Edge Cases:** Include tests for special/long/unicode names, empty names, and error conditions.
- **Death/Error Tests:** Use `EXPECT_DEATH` and dedicated fixtures for assertion/death/error scenarios.

## 5. Comprehensive Assertions

- **Strong Assertions:** Use `EXPECT_*`, `ASSERT_*`, and custom failure messages for clarity.
- **Validation:** Check both state (e.g., node validity, scene emptiness) and side effects (e.g., node count, invalidation).

## 6. Consistent Naming and Style

- **Test Naming:** Use descriptive names following the pattern `TestCase_WhatIsTested`.
- **Variable Naming:** Name variables clearly and consistently (e.g., `node1`, `parent`, `child_opt`).
- **Commenting:** Add comments to explain intent, especially for non-obvious logic or test setup.

## 7. Coverage of API Surface

- **API Coverage:** Cover all major public API methods, including normal, boundary, and error cases.
- **Negative Testing:** Include tests for invalid input, destroyed nodes, and cross-scene operations.

## 8. Portability and Robustness

- **No Global State:** Manage all state within fixtures or local to tests.
- **No Hardcoded Dependencies:** Set scene/node names and counts per test, not globally.

## 9. Test Development Discipline

### **CRITICAL: Never Change Implementation to Fix Tests**

- **Test-First Mindset:** When tests fail, first examine whether the test expectations are correct, not the implementation.
- **Root Cause Analysis:** Always identify the real reason for test failure before making any changes.
- **Implementation Changes:** Only modify the System Under Test (SUT) when there's a genuine bug in the implementation, never to make tests pass.
- **Approval Required:** Get explicit approval before changing any implementation code during test development.

### **Common Test Issues and Solutions**

- **State Setup Problems:** Ensure proper initialization sequence (e.g., call `UpdateSceneTransforms()` before testing transform preservation).
- **Invalid Test Parameters:** Verify test parameters match the intended test scenario (e.g., `preserve_world_transform = false` for basic functionality tests).
- **Missing Prerequisites:** Check that all required setup steps are performed before the action being tested.

---

### Summary Table

| Aspect                | Practice/Pattern                                                                 |
|-----------------------|----------------------------------------------------------------------------------|
| Structure             | Grouped by functionality, section headers, anonymous namespace                   |
| Fixtures              | Setup/teardown, shared helpers, multiple fixtures for different test types       |
| Helpers               | Encapsulate common logic, improve readability                                    |
| Test Cases            | AAA pattern, single responsibility, edge/error cases, death tests                |
| Assertions            | Strong, descriptive, custom failure messages                                     |
| Naming/Style          | Descriptive names, consistent variable naming, clear comments                    |
| API Coverage          | Normal, boundary, error, and cross-object tests                                  |
| Robustness            | No global state, portable, no hardcoded dependencies                             |
| **Test Discipline**   | **Never change SUT to fix tests without root cause analysis, proper state setup**      |

---

## How to Apply These Guidelines

- Use fixtures and helpers for setup and common logic.
- Group tests logically and use clear section headers.
- Write focused, single-purpose tests with strong assertions.
- Cover both normal and edge/error cases.
- Use descriptive naming and thorough commenting.
- Ensure tests are robust, portable, and maintainable.
- **NEVER modify implementation to make tests pass - fix the test or get approval first.**
- **Always verify proper state setup before testing complex scenarios (e.g., transforms).**

Following these practices will ensure clarity, maintainability, comprehensive coverage, and proper testing discipline in your test suites.
