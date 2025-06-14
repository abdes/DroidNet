# Oxygen Engine Documentation Rules

This document defines the documentation standards applied to the Oxygen Engine codebase, specifically derived from the SceneQuery implementation patterns.

## Comment Markers and Doxygen Style

### Basic Comment Types
- Use `//!` for brief documentation comments above the declaration
- Use `/*!` multi-line comments for detailed documentation blocks
- Use `@command` style (not `\command`) for all Doxygen directives
- Never use `//!<` inline comments due to 80-column formatting constraints

### Header Structure
```cpp
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
```

### Section Headers
Use decorative comment blocks for major sections. The ending / must be at column
80 exatcly after formatting.
```cpp
//=== Query Result Types ===--------------------------------------------------//
//=== High-Performance Scene Graph Query ===----------------------------------//
//=== Private Implementation Helpers ===--------------------------------------//
```

## Documentation Placement Rules

### Inline/Template Methods
For methods implemented inline in the header (including all template methods):
- **Keep full documentation with the method declaration**
- Include both brief description and detailed documentation blocks
- Place all usage examples, performance notes, and cross-references in header

### Methods Implemented in .cpp Files
For methods with implementations in separate .cpp files:
- **Header**: Keep only brief `//!` description with the declaration
- **.cpp**: Place detailed documentation block immediately before implementation
- Move all detailed explanations, examples, and performance characteristics to .cpp

### Example Pattern

**Header (.h file):**
```cpp
//! Find first node by absolute path from scene root
auto FindFirstByPath(std::string_view path) const noexcept
  -> std::optional<SceneNode>;
```

**.cpp file:**
```cpp
/*!
 Navigates the scene hierarchy using an absolute path specification,
 starting from scene root nodes. Optimized for simple paths without wildcards.

 @param path Absolute path string (e.g., "World/Player/Equipment/Weapon")
 @return Optional SceneNode if path exists, nullopt if path not found

 ### Path Navigation
 - Uses direct parent-child navigation for simple paths (O(depth) complexity)
 - Falls back to traversal-based search for wildcard patterns (O(n) complexity)

 ### Usage Examples
 ```cpp
 auto weapon = query.FindFirstByPath("World/Player/Equipment/Weapon");
 ```
*/
auto SceneQuery::FindFirstByPath(std::string_view path) const noexcept
  -> std::optional<SceneNode>
```

## Member Documentation

### Struct/Class Members
Place documentation above each member (not inline) to comply with 80-column limit:
```cpp
struct QueryResult {
  //! Number of nodes examined by the filter
  std::size_t nodes_examined = 0;
  //! Number of nodes that matched the query criteria
  std::size_t nodes_matched = 0;
  //! true if query completed successfully, false if interrupted
  bool completed = true;
};
```

### Enum Values
Document enum values with inline comments:
```cpp
enum class Type : uint8_t {
  //! Find first matching node with early termination
  kFindFirst,
  //! Collect all matching nodes to container
  kCollect,
  //! Count matching nodes without allocation
  kCount,
  //! Check existence with early termination
  kAny,
} type;
```

## Content Structure

### Method Documentation Template
/*!
 Detailed description explaining what the method does and its purpose.

 @tparam TemplateParam Description of template parameters
 @param parameter_name Description of parameter
 @return Description of return value

 ### Performance Characteristics
 - Time Complexity: O(n) description
 - Memory: Allocation behavior
 - Special optimizations

 ### Usage Examples
 ```cpp
 // Example usage with explanation
 auto result = method(arguments);
 ```

 @note Important notes about usage
 @warning Critical warnings about limitations or safety
 @see RelatedMethod, RelatedClass for cross-references
*/

### Class/Struct Documentation Template
//! Brief description of the class purpose
/*!
 Detailed description of the class functionality and design.

 ### Key Features
 - **Feature 1**: Description with emphasis
 - **Feature 2**: Another important feature

 ### Usage Patterns
 Description of how the class is intended to be used.

 ### Architecture Notes
 Important design decisions or architectural considerations.

 @warning Important safety or usage warnings
 @see RelatedClasses for related functionality
*/
class ExampleClass {

## Formatting Rules

### Line Length
- Hard limit of 80 columns for all content
- Break long descriptions across multiple lines
- Use proper indentation for continued lines

### Code Examples
- Wrap code examples in triple backticks with cpp language specifier
- Keep examples concise but complete
- Include explanatory comments in examples when helpful

### Special Sequences
- Escape wildcard sequences in path examples: `"*\/Enemy"`, `"**\/Weapon"`
- Use markdown-style formatting for emphasis: `**bold**`, `*italic*`
- Use backticks for inline code: `` `methodName()` ``

### Cross-References
- Use `@see` for referencing related methods/classes
- Use `@note` for important usage information
- Use `@warning` for critical safety or limitation information
- Reference parameters with `@param` and return values with `@return`

## Performance Documentation

### Always Include When Relevant
- Time complexity (Big O notation)
- Memory allocation behavior
- Special optimization strategies
- Performance comparisons between different approaches

### Example Format
```cpp
### Performance Characteristics
- Time Complexity: O(n) for full scene traversal
- Memory: Zero allocations during search
- Optimization: Early termination via VisitResult::kStop
```

## Cross-Reference Standards

### Method References
- Always reference related methods using `@see`
- Include the class name when referencing methods from other classes
- Group related references logically

### Example Cross-References
```cpp
@see ExecuteBatch for batch processing multiple FindFirst operations
@see Scene::Query() for obtaining a SceneQuery instance
@note This method automatically routes to batch execution when called within ExecuteBatch()
```

## Documentation Maintenance

### Consistency Requirements
- All public APIs must have complete documentation
- Private methods need only brief descriptions unless complex
- Template parameters must be documented with `@tparam`
- All parameters and return values must be documented

### Update Guidelines
- Update documentation when method signatures change
- Move documentation when refactoring between header/implementation
- Maintain consistency in terminology across related methods
- Update cross-references when methods are renamed or moved
