---
applyTo: '**/*.cpp;**/*.h'
---
# DOC COMMENTS RULES

## OVERVIEW

These rules define how to generate and maintain documentation for the Oxygen
Engine codebase. All standards are derived from SceneQuery implementation
patterns. Apply these rules to all public APIs, and to private methods as
specified.

## COMMENT TYPES AND DOXYGEN STYLE

- NEVER USE `/*` block comments inside code examples.
- ALWAYS ESCAPE '*' and '/' in strings inside code examples.
- After generating or editing a doc comment, re-scan your output to ensure compliance.
- Use `//!` for brief, single-line documentation above declarations.
- Use `/*! ... */` for detailed, multi-line documentation blocks.
- Use `@command` (not `\command`) for all Doxygen directives.
- Do NOT use `//!<` inline comments (enforce 80-column limit).

## HEADER AND SECTION FORMATTING

- Insert the following header at the top of each file:
  //===----------------------------------------------------------------------===//
  // Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
  // copy at <https://opensource.org/licenses/BSD-3-Clause>.
  // SPDX-License-Identifier: BSD-3-Clause
  //===----------------------------------------------------------------------===//
- For major code sections, use decorative comment blocks. The ending '/' must be at column 80:
  //=== Section Name ===-------------------------------------------------------------//

## DOCUMENTATION PLACEMENT RULES

- Identify where the method or freeform function implementation is located.
- For functions only declared, or methods with only a delcaration inside a class:
  - Above method declaration: Only a brief `//!` description.
- For functions implemented inline, or methods implemented inside the class declaration scope:
  - Place full documentation (brief + detailed) with the method declaration.
  - Include usage examples, performance notes, and cross-references in the header.
- For function implementation in .cpp, or methods implemented outside the class
  declaration scope in .h or .cpp:
  - Above implementation: Place the detailed documentation block.
- Insert a blank line before the doc comment block.

## CONTENT STRUCTURE TEMPLATES

- For methods detailed content, use this template (vertical allignment of content on the *, i.e. content lines are indented with one space only from left edge of block):

  /*!
   Detailed description of the method.

   @tparam TemplateParam Description
   @param parameter_name Description
   @return Description

  ### Performance Characteristics

  - Time Complexity: ...
  - Memory: ...
  - Optimization: ...

  ### Usage Examples

   ```cpp
   // Example usage
   auto result = method(arguments);
   ```

   @note ...
   @warning ...
   @see RelatedMethod, RelatedClass
  */

- For classes/structs, use this template:

  //! Brief description
  /*!
   Detailed description.

  ### Key Features

  - **Feature 1**: ...
  - **Feature 2**: ...

  ### Usage Patterns

   ...

  ### Architecture Notes

   ...

   @warning ...
   @see ...
  */

## FORMATTING RULES

- Enforce a hard 80-column limit for all content.
- Break long lines and indent continued lines properly.
- Wrap code examples in triple backticks with cpp language specifier.
- Keep code examples concise and explanatory.
- Escape wildcard sequences in path examples: "*/Enemy", "**/Weapon".
- Use markdown for emphasis: `**bold**`, `*italic*`, and backticks for inline code.

## CROSS-REFERENCES AND TAGS

- Use `@see` for related methods/classes (include class name if from another class).
- Group related references logically.
- Use `@note` for important usage info.
- Use `@warning` for critical safety/limitation info.
- Reference parameters with `@param` and return values with `@return`.
- Use `@throw` to document exceptions thrown by a function, if any. Example: `@throw std::invalid_argument if the input is invalid.`

## PERFORMANCE DOCUMENTATION

- Always document time complexity (Big O), memory allocation, and optimizations when relevant.
- Use this format:

  ### Performance Characteristics

  - Time Complexity: ...
  - Memory: ...
  - Optimization: ...

## MAINTENANCE AND CONSISTENCY

- All public APIs must have complete documentation.
- Private methods need only brief descriptions unless complex (if a private method has nontrivial logic, document it with a detailed comment block as for public APIs).
- Document all template parameters with `@tparam`.
- Document all parameters and return values.
- Document all exceptions thrown with `@throw` if applicable.
- Update documentation when method signatures change or code is refactored.
- Move documentation as needed when relocating code between header/implementation.
- Maintain consistent terminology and update cross-references on renames/moves.

## LANGUAGE AND AUTOMATION

- All documentation comments must be written in English.
- If automated tools are used to check documentation coverage or formatting, ensure compliance before submitting changes.
