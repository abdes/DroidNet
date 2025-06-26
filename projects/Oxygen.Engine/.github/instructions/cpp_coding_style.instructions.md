---
applyTo: '**/*.cpp;**/*.h'
---
# C++ CODING STYLE INSTRUCTIONS

## GENERAL RULES

- Follow the Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html
- Use C++20 features and standard library only (no compatibility with older standards).
- Use `#pragma once` for include guards in all headers.
- Prefix all standard library types and functions with `std::`.
- Use `#include <...>` syntax for all includes.
- Use designated initializers for struct initialization whenever possible.
- Use trailing commas in aggregate initializations (e.g., structs, arrays, enums) when applicable.
- All new files must start with the BSD license preamble:
  //===----------------------------------------------------------------------===//
  // Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
  // copy at https://opensource.org/licenses/BSD-3-Clause.
  // SPDX-License-Identifier: BSD-3-Clause
  //===----------------------------------------------------------------------===//
- ALWAYS preserve empty lines that exist in the original code, even if they are not needed for formatting.

## NAMING CONVENTIONS

- **File Names:** Follow existing files naming patterns. Example: `MyClass.h`, `MyClass_basic_test.cpp`
- **Type Names (classes, structs, enums, typedefs):** Use UpperCamelCase. Example: `MyClass`
- **Variable Names:** Use lower_case_with_underscores. Example: `my_variable`
- **Constant Names:** Use kUpperCamelCase. Example: `kMaxValue`
- **Function Names:** Use UpperCamelCase. Example: `ComputeValue()`
- **Namespace Names:** Use all lowercase, words joined by underscores. Example: `my_namespace`
- **Enumerator Names:** Use kUpperCamelCase. Example: `kFirstValue`
- **Template Parameter Names:** Use UpperCamelCase or single capital letters. Example: `typename T`
- **Macro Names:** Use all uppercase with underscores. Example: `MY_MACRO_NAME`
- **Class Data Members:** Name like ordinary nonmember variables, but with a trailing underscore (e.g., `my_member_`). Static constant class members follow the rules for constants (e.g., `kMaxValue`).
- **Struct Data Members:** Name like ordinary nonmember variables (e.g., `my_member`). Do not use trailing underscores for struct members.

## CODE QUALITY RULES

- Use `constexpr`, `noexcept`, and `auto` return types where appropriate.
- Prefer compiler auto-deduction for return types unless clarity or reference return type is needed.
- Do not use `auto` for void return types.
- Use OXYGEN macros (e.g., OXYGEN_MAKE_NON_COPYABLE, OXYGEN_DEFAULT_COPYABLE, OXYGEN_DEFAULT_MOVABLE) for common patterns.
- Follow the Rule of 5 for all classes that define any special member function.
- Prefer modern C++ idioms; avoid legacy patterns.
- Ensure all code is robust, clear, and maintainable.

## DOC COMMENTS RULES

- Use rules from .github/instructions/DOC_INSTRUCTIONS.md when writing doc comments.

## UNIT TEST WRITING RULES

- Use rules from .github/instructions/TEST_INSTRUCTIONS.md when writing unit tests.
