# M07B redirected native progress

Date: 2026-09-15

The import/replacement captures exposed cursor-control sequences and truncated
progress paths in Cooking. ImportTool's console writer now detects redirected
stdout. Pipes receive full newline-delimited progress without cursor controls,
spinner padding or terminal-width clipping; interactive terminal presentation
is retained for an actual terminal.

Validation:

- CMake builds succeed in Debug and Release, and both SDKs are installed.
- Native CLI imports with stdout/stderr captured as pipes succeed in both
  configurations. Neither stream contains an escape character; the longest
  captured line is 217 characters.
- Rebuilt Release Interop/UI tests assert that successful native import messages
  contain no escape characters and preserve geometry-progress paths longer than
  80 characters. Import/replacement, retry, save-conflict and project-closure
  cases pass **7/7** together.

Evidence: `artifacts/m07b-plain-import/results.json`, its Debug/Release logs, and
`artifacts/TestResults/m07b-plain-output-save-recovery.trx`.
Native build/install logs use the `artifacts/m07b-plain-import-` prefix.
