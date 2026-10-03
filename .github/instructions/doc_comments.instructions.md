---
applyTo: "**/*.cpp;**/*.h"
---

# Oxygen.Engine documentation comments

- Follow the owning module's style and
  [engineering rules](../../design/oxygen/RULES.md). Do not replace license headers
  or impose these Doxygen conventions on C++/CLI XML documentation.
- Use `//!` above brief declarations and `/*! ... */` for detailed documentation;
  use `@` directives, not backslash directives. Avoid inline `//!<` comments.
  Keep English prose within the engine formatter's 80-column limit.
- For out-of-line definitions, keep a brief declaration comment and place details
  above the definition. Inline definitions carry their full documentation there.
  Preserve the module's established indentation and logical section spacing.
- Document public contracts: parameters/template parameters, non-void results,
  ownership, lifetime, failure modes and actual exceptions. Explain non-obvious
  private logic, not every trivial helper. Update references when symbols change.
- Include complexity, allocation and performance notes only when relevant and
  supported by the implementation; do not invent guarantees or mandatory sections.
  Examples should illustrate real APIs, not repeat the signature.
- Use `@param`, `@tparam`, `@return`, `@throw`, `@note`, `@warning` and `@see` as
  applicable. Fenced examples use `cpp`; inside block comments, avoid nested block
  comments and literal `*/`, even inside example strings, because they terminate
  the outer comment. Do not blanket-escape ordinary `*` or `/` characters.
- Review edited comments for accurate contracts, valid references and balanced
  delimiters. Documentation-only tasks must not alter implementation behavior.
