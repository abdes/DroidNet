---
applyTo: '**/*.cpp;**/*.h'
---
- Follow the Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html
- Use C++20 features and standard library. No compatibility with older standards is required.
- Use `#pragma once` for include guards.
- Use `std::` prefix for all standard library types and functions.
- Use `#include <...>` for all include files.
- All new files must have the BSD license preamble as shown above.

Unit Tests:
- Use the Google Test framework (not Catch2).
- Test cases must be scenario-based, not function-based.
- Name test cases clearly and descriptively.
- Use GTest macros and place tests in anonymous namespaces.

Documentation:
1. Use Doxygen style comments.
   - Use `//!` for single line comments (ended with a dot) and `/*! ... */` for multi-line comments.
   - The brief description should be on its own line starting with `//!`.
   - Indentation is important.
   - Example:
     //! Barrier description for memory operations synchronization.
     /*!
      Memory barriers ensure visibility of memory operations across the GPU pipeline
      without requiring explicit state transitions.
     */
2. Use `//!<` for inline comments.
3. Write concise and clear documentation for all public functions, classes, and variables, providing critical information not obvious from the code itself. Avoid duplication.
4. Use proper punctuation and grammar.
5. Do not use explicit `brief` tags.
6. Use `\param` for function parameters, `\return` for return values, `\throws` for exceptions, and `\note` for additional notes.
7. Omit detailed documentation for simple functions and classes.
8. Start with `/*!` for multi-line comments.
9. Indent subsequent lines by exactly 1 space from the `/*!` line.
10. Do not use leading asterisks (*) for each line.
11. Align the closing `*/` with the opening `/*!` of the same line.

Code Quality:
- Use `constexpr`, `noexcept`, and `auto` return types where appropriate.
- Do not specify the return type, let the compiler auto deduce it, unless you
  must or it will bring clarity or specify reference return type, etc.
- Do not use auto for void return types.
- Use OXYGEN macros (e.g., OXYGEN_MAKE_NON_COPYABLE, OXYGEN_DEFAULT_COPYABLE,
  OXYGEN_DEFAULT_MOVABLE) for common patterns.
- Follow the Rule of 5 for all classes that define any special member function.
- Prefer modern C++ idioms and avoid legacy patterns.
- All code must be robust, clear, and maintainable.

All new files must have the preamble:
```cpp
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
```
