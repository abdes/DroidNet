# TextWrap Module User Guide

The `TextWrap` module provides a flexible, robust, and highly configurable text
wrapping utility. It is designed for formatting text into lines of a specified
width, with advanced options for whitespace handling, indentation, and
hyphenation. Typical use cases include command line help formatting, pretty
formatting of fized width text descriptions, etc.

---

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [API Reference](#api-reference)
- [Configuration Options](#configuration-options)
- [Usage Examples](#usage-examples)
- [Design Notes](#design-notes)
- [Implementation Details](#implementation-details)
- [Extending TextWrap](#extending-textwrap)
- [FAQ](#faq)

---

## Overview

`TextWrap` is a C++20 module for wrapping and formatting text. It supports:

- Optimal line breaking (not greedy)
- Indentation for first and subsequent lines
- Tab expansion
- Whitespace collapsing and trimming
- Optional breaking on hyphens
- Paragraph-aware wrapping

All configuration is done via a fluent builder API, ensuring clarity and
correctness.

---

## Key Features

- **Optimal Wrapping:** Uses a dynamic programming algorithm to minimize
  raggedness and balance line lengths.
- **Fluent Builder API:** Configure all options in a readable, chainable way.
- **Whitespace Control:** Collapse or trim whitespace as needed.
- **Indentation:** Set different prefixes for the first and subsequent lines.
- **Tab Expansion:** Replace tabs with custom strings.
- **Paragraph Support:** Handles multi-paragraph input, preserving paragraph
  breaks.
- **Hyphenation:** Optionally break lines at hyphens in compound words.

---

## Future Enhancements

The following features are planned or under consideration to improve terminal
help display and general usability. Status is tracked below:

| Feature                       | Status | Notes |
|-------------------------------|--------|-------|
| Unicode Awareness             | ❌     | Width is measured in code units, not display columns. |
| ANSI Escape Code Handling     | ❌     | No support for ignoring ANSI color/formatting codes in width calculation. |
| Paragraph and List Formatting | ❌     | Paragraphs supported, but no explicit list/bullet formatting. |

Legend: ✅ = Implemented, ❌ = Not implemented

---

## API Reference

### Main Classes

- `oxygen::wrap::TextWrapper` — The core class for wrapping text.
- `oxygen::wrap::TextWrapperBuilder` — Fluent builder for configuring and
  creating `TextWrapper` instances.

### Construction

Create a wrapper using the builder:

```cpp
using oxygen::wrap::MakeWrapper;

auto wrapper = MakeWrapper()
    .Width(60)
    .Initially("  ")
    .Then("    ")
    .CollapseWhiteSpace()
    .TrimLines();
```

### Wrapping Text

- `Wrap(const std::string&) -> std::optional<std::vector<std::string>>`
  - Wraps input text into lines, returns a vector of lines (no trailing
    newlines).
- `Fill(const std::string&) -> std::optional<std::string>`
  - Wraps input text and returns a single string with lines joined by `\n`.

### Example

```cpp
std::string text = "This is a long paragraph that should be wrapped optimally.";
auto lines_opt = wrapper.Wrap(text);
if (lines_opt) {
    for (const auto& line : *lines_opt) {
        std::cout << line << '\n';
    }
}

auto filled_opt = wrapper.Fill(text);
if (filled_opt) {
    std::cout << *filled_opt << std::endl;
}
```

---

## Configuration Options

All options are set via the builder API:

| Method                | Description                                       | Default     |
|-----------------------|---------------------------------------------------|-------------|
| `Width(size_t)`       | Maximum line width                                | 80          |
| `IndentWith()`        | Start indentation configuration (see below)       |             |
| `Initially(str)`      | Indent for the first line                         | ""          |
| `Then(str)`           | Indent for subsequent lines                       | ""          |
| `ExpandTabs(str)`     | Replace tab characters with this string           | "\t"        |
| `CollapseWhiteSpace()`| Collapse contiguous whitespace to a single space  | false       |
| `TrimLines()`         | Trim whitespace at start/end of each line         | false       |
| `BreakOnHyphens()`    | Allow breaking lines at hyphens                   | false       |

**Indentation Example:**

```cpp
auto wrapper = MakeWrapper()
    .Width(50)
    .IndentWith()
    .Initially("* ")
    .Then("  ");
```

---

## Usage Examples

### Basic Wrapping

```cpp
auto wrapper = MakeWrapper().Width(40);
std::string text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor.";
auto result = wrapper.Fill(text);
if (result) {
    std::cout << *result << std::endl;
}
```

### Indented, Trimmed, and Collapsed Whitespace

```cpp
auto wrapper = MakeWrapper()
    .Width(30)
    .IndentWith().Initially("-> ").Then("   ")
    .CollapseWhiteSpace()
    .TrimLines();

std::string messy = "   This   line\t\t has   lots   of   spaces.  ";
auto lines = wrapper.Wrap(messy);
if (lines) {
    for (const auto& l : *lines) std::cout << l << '\n';
}
```

### Breaking on Hyphens

```cpp
auto wrapper = MakeWrapper().Width(20).BreakOnHyphens();
std::string hyphenated = "state-of-the-art technology is ever-evolving.";
auto filled = wrapper.Fill(hyphenated);
if (filled) std::cout << *filled << std::endl;
```

---

## Design Notes

- **Builder Pattern:** All configuration is done via `TextWrapperBuilder`,
  enforcing valid, consistent options.
- **Optimal Wrapping:** Uses a dynamic programming algorithm to minimize the sum
  of squared extra spaces at line ends, producing visually balanced text.
- **Separation of Concerns:** Tokenization, wrapping, and configuration are
  modular and independent.
- **Paragraph Handling:** Multiple paragraphs are separated by empty lines in
  the output.

---

## Implementation Details

- **Tokenization:** Input is split into tokens (words, whitespace, newlines,
  paragraph marks) using an internal tokenizer.
- **Algorithm:** The core algorithm (`WrapChunks`) computes the optimal line
  breaks for a sequence of tokens, considering all configuration options.
- **Whitespace/Indentation:** Handles trimming, collapsing, and indentation as
  post-processing steps.
- **Performance:** Designed for efficiency, using preallocated vectors and
  minimizing string copies.

---

## Extending TextWrap

- To add new whitespace or tokenization rules, extend the internal tokenizer.
- For new wrapping strategies, add new methods to `TextWrapper` or provide
  alternative algorithms.
- For custom output formatting, post-process the result of `Wrap()` or `Fill()`.

---

## FAQ

**Q: What happens if a word is longer than the line width?** A: The word will be
placed on its own line, even if it exceeds the width.

**Q: How are paragraphs separated?** A: By one or more empty lines in the input;
output will have a single empty line between paragraphs.

**Q: Can I use Unicode or non-ASCII text?** A: Yes, as long as the input is a
valid `std::string`. Width is measured in code units, not grapheme clusters.

**Q: Is the module thread-safe?** A: `TextWrapper` instances are immutable after
construction and can be used concurrently.

---

## See Also

- [TextWrap.h](TextWrap.h) — API and documentation
- [TextWrap.cpp](TextWrap.cpp) — Implementation
