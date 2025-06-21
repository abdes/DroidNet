---
applyTo: '**/*.cpp;**/*.h'
---
//=== GENERAL RULES ===----------------------------------------------------------------//
- Follow the Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html
- Use C++20 features and standard library only (no compatibility with older standards).
- Use `#pragma once` for include guards in all headers.
- Prefix all standard library types and functions with `std::`.
- Use `#include <...>` syntax for all includes.
- All new files must start with the BSD license preamble:
  //===----------------------------------------------------------------------===//
  // Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
  // copy at https://opensource.org/licenses/BSD-3-Clause.
  // SPDX-License-Identifier: BSD-3-Clause
  //===----------------------------------------------------------------------===//

//=== CODE QUALITY RULES ===-----------------------------------------------------------//
- Use `constexpr`, `noexcept`, and `auto` return types where appropriate.
- Prefer compiler auto-deduction for return types unless clarity or reference return type is needed.
- Do not use `auto` for void return types.
- Use OXYGEN macros (e.g., OXYGEN_MAKE_NON_COPYABLE, OXYGEN_DEFAULT_COPYABLE, OXYGEN_DEFAULT_MOVABLE) for common patterns.
- Follow the Rule of 5 for all classes that define any special member function.
- Prefer modern C++ idioms; avoid legacy patterns.
- Ensure all code is robust, clear, and maintainable.

//=== DOC COMMENTS RULES ===-----------------------------------------------------------//
- Use rules from .github/instructions/DOC_INSTRUCTIONS.md when writing doc comments.

//=== UNIT TEST WRITING RULES ===-----------------------------------------------------------//
- Use rules from .github/instructions/TEST_INSTRUCTIONS.md when writing unit tests.
