---
applyTo: '**/*.cpp;**/*.h'
---
Follow Google C++ Style Guide for all C++ code. The guide can be found at:
https://google.github.io/styleguide/cppguide.html.
Assume you are using C++20 and maximize the use of C++20 features and the
standard library. Do not attempt to keep compatibility with older C++ standards.
Use `#pragma once` as the include guard. Use `std::` prefix for all standard
library types and functions.
Use `#include <...>` for all include files.
When writing unit test cases, use the Google Test framework, ensure test cases
are scenario based and not function based and name the test cases in a clear and
descriptive way.

When writing documentation:
1. Use Doxygen style comments. Use `//!` for single line comments (ended with a
   dot) and `/*! ... */` for multi-line comments.
2. Use `//!<` for inline comments.
3. Write concise and clear documentation for all public functions,
   classes, and variables, that provides critical information to the maintainer
   or the user of the API, which is not obviously visible in the code itself,
   and avoid duplication of information.
4. Use proper punctuation and grammar.
5. Do not use explicit `brief` tags.
6. Use `\param` for function parameters, `\return` for return values, `\throws`
   for exceptions, and `\note` for additional notes.
7. Omit detailed documentation for simple functions and classes.
8. Start with /*!
9. Have subsequent lines indented by exactly 1 space from the `/*!` line.
10. Not use leading asterisks (*) for each line
11. Have the closing `*/` aligned with the opening `/*!` of the same line.
