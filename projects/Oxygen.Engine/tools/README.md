# Oxygen developer tools

## Repository ownership policy

The engine root's `.oxytools.json` defines first-party file ownership for developer
tools. It contains two required fields:

- `project_roots`: nonempty array of directory paths, resolved against the target
  engine root. Resolved paths must remain inside that root.
- `exclude`: array of glob patterns matched against engine-relative paths using
  forward slashes. A match excludes the file even if it is under a project root.
  Matching is case-sensitive and follows Python `fnmatchcase` semantics.

Both arrays contain strings; unknown fields are rejected. The checked-in policy
owns `src/Oxygen` and `Examples`, with exclusions for vendored files and dependency
directories. Build outputs outside those roots are not owned.

This policy determines ownership, not which operation a tool performs. File-type
selection, test inclusion, and execution options belong to each tool. Diagnostic
rules remain in `.clang-tidy`; formatting rules remain in `.clang-format`.

Oxytidy reads this shared policy for ownership validation and checkout discovery.
Its explicit ownership override and target/tool-checkout policy precedence are
documented in [the oxytidy reference](oxytools/docs/oxytidy.md#select-another-engine-checkout).

The [shared Python distribution](oxytools/README.md) provides both `oxytidy` and
`oxyformat`. Ownership validation, atomic file writes, and Windows process
ownership are implemented once in its `oxytools` package. Analysis and formatting
remain independent command implementations.

The shared file replaces `.oxytidy.json`; the old filename is no longer discovered
automatically. Existing custom policies can still be selected explicitly through
oxytidy's `--ownership-file` option. The migration preserves the policy contents
and oxytidy's scope, exclusions, and test-inclusion behavior.
