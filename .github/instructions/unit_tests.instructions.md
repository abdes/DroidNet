---
applyTo: "**/*.cpp;**/*.h"
---

# Native tests

- These GoogleTest conventions apply to Oxygen.Engine, not the editor's native
  interop VSTest projects. Follow the owning suite and
  [engine workflow](../../projects/Oxygen.Engine/AGENTS.md).
- Include `<Oxygen/Testing/GTest.h>`, not GoogleTest/GoogleMock headers directly.
  Use its `NOLINT_TEST*`, typed-test, throw and death wrappers as applicable;
  use GoogleMock only when the behavior needs it.
- Wrap assertion helpers with `GCHECK_F`; use `TRACE_GCHECK_F` with a concise tag
  when failure context would otherwise be ambiguous, not `SCOPED_TRACE` directly.
- Test observable behavior and recovery, not private call sequences. Separate
  arrange/act/assert and briefly document intent with `//!` or `/*! ... */`.
  Follow existing suite/test naming and place test definitions in anonymous
  namespaces where compatible with the suite's registration.
- Keep mutable state fixture-owned or local. Split fixtures only when lifecycle
  or setup differs; do not create one fixture for every scenario category.
  Reuse owning helpers for assertions and resource setup/cleanup.
- Prefer collection matchers over hand-written assertion loops. Add custom
  messages only for missing context; use fatal assertions when continuing would
  make the test invalid or unsafe.
- Build the owning executable before CTest; `-R` selects executables, whereas
  `--gtest_filter=Suite.Case` selects individual cases. GPU CTest resource locks
  coordinate one CTest run, not simultaneous runs or other applications.
