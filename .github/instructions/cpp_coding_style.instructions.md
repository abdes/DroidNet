---
applyTo: "**/*.cpp;**/*.h"
---

# C++ conventions

- For Oxygen work, follow [engineering rules](../../design/oxygen/RULES.md).
  Repository/module configuration takes precedence over generic Google style.
  Engine internals may use C++23; editor C++/CLI and native-command consumers
  require C++20-compatible API surfaces and transitive headers.
- Write compliant code from the first edit. Use the owning module's formatter;
  do not preserve incidental whitespace against its rules or chase diagnostics
  through repeated formatting/reordering attempts.
- Match the owning project's license header: Oxygen.Engine is BSD-3-Clause,
  not a license mandate for every C++ project in this monorepo.

## Oxygen.Engine specifics

- Use `#pragma once`, angle-bracket includes and `std::` qualification except
  the established `<cstdint>` names. Follow `.clang-format` include groups:
  standard library, third-party/platform, then Oxygen; keep the matching source
  header in the Oxygen group. Respect configured prerequisite-header exceptions.
- Protect genuinely order-sensitive includes with `clang-format off/on` and a
  reason; use `IWYU pragma: keep` for prerequisites include-cleaner must retain.
  Formatting markers do not disable analysis or include removal.
- Use trailing commas in multiline initializers/enums, not single-line ones.
  Initialize required aggregate fields; use designated initializers when suitable.
- Types/functions/template parameters use UpperCamelCase; constants/enumerators
  use `kUpperCamelCase`; variables/namespaces use snake_case. Class fields have a
  trailing underscore; struct fields do not. Unwrapped NamedType locals use `u_`.
- Use `NamedType` at meaningful domain/API boundaries, with an inline `<Type>Tag`
  and only the skills needed. Inspect its definitions; do not mandate formatting
  suppression or add strong types for otherwise clear implementation locals.
- Use Base's copy/move macros and complete special-member semantics. Export
  individual methods with the module's `OXGN_*` API macros, not entire classes;
  inspect `*_NDAPI` and exception specifications rather than guessing.
- Use `oxygen::serio` for binary serialization/deserialization; cooked layouts belong
  to `Oxygen.Data`. Do not invent parallel byte-packers. This does not prohibit
  the checked, alignment-safe `memcpy` needed for mapped GPU or binary data.
- See [doc comments](doc_comments.instructions.md) and
  [native tests](unit_tests.instructions.md) for the corresponding conventions.
