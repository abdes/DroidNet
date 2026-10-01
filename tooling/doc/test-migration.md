# MSTest migration

Status: rollout implemented; focused corrections are being committed. All 43 test projects and four examples are migrated.
The Debug build and focused failure checks pass. The repository owner runs
full-suite validation in Visual Studio; agent checks are limited to affected cases. See [scope](#scope), [commit-boundaries](#commit-boundaries),
[validation](#validation), and [release tracking](https://github.com/abdes/DroidNet/issues/19).

## Scope

- Migrate 43 C# test projects to the pinned MSTest SDK, including 14 WinUI suites.
  Native C++ tests retain their supported Visual Studio runner. Engine CMake is
  outside this work.
- Use the validated shared UI host: dispatcher available at startup, one window
  created only for realized content, reuse between cases, and runner-owned exit.
  Preserve fixture setup/cleanup, parallelism declarations, UI dispatch, rendering
  and visual-state waits. Host-lifecycle regression cases stay in the two pilot
  consumers; they must not force a window into dispatcher-only suites.
- Consolidate the six independent startup implementations: Bootstrap, Hosting,
  Converters, Mvvm, Mvvm.Generators integration and OutputLog. Update all four test
  project examples, including both UI templates, so new projects follow the same pattern.
- Choose hosting from test requirements. The source audit found no explicit
  package-identity assertions in the current UI suites; qualify their resources
  and native dependencies unpackaged. Keep any demonstrated identity-dependent
  coverage in an explicitly packaged host, not a duplicate of every UI suite.
- Remove superseded manual runner bootstraps, SDK-owned package/property
  duplication, unused test MSIX assets and local/CI hosting differences. Keep
  product application packaging separate from test-host configuration.

Package-sensitive production paths in Resources asset resolution and Project
Browser thumbnail loading currently lack direct tests. Track that coverage gap
without describing the component migration as packaged-application validation.

## Commit boundaries

Source branch: `codex/ed-m08.1-canonical-data`.
Implementation branch: `codex/droidnet-build-streamlining`.

| Order | Commit                        | Contents                                                                                                                                           |
| ----- | ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1     | General infrastructure        | Build/analysis separation, explicit packaging, artifact paths, SlnGen, tooling commands and shell setup; associated docs/tests                     |
| 2     | Test-project migration        | SDK/feed pin, project and template migration, shared UI host/lifecycle tests, removal of unused runner/packaging code; migration and workflow docs |
| 3     | Reusable-library test fixes   | Collections notifications, Coordinates compile/thread checks, Mvvm fixture registration and Routing resource isolation                             |
| 4     | Editor test fixtures          | Generator diagnostic hashes/newline assertions, repository discovery, browser publication/navigation fixtures and native camera expectations       |
| 5     | Content-browser behavior      | Disable folder cooking for derived mounts; corresponding built-in ownership/action tests                                                           |
| 6     | Desktop-independent UI checks | Replace global input tests with color edit-session checks; retain numeric cancellation coverage and verify import-review tab order                 |

Preserve existing baseline failures in the record until diagnosed. Fix a product
defect in its owning module when tests expose one; do not hide it by weakening the
test. Keep those fixes separate from the migration commit as well.

Keep these boundaries in history. After merge approval, merge the implementation
branch into the source branch using `git merge --no-ff`. The merge must preserve
the internal commit sequence; do not squash the migration into the source branch.

## Validation

| Boundary          | Required check                                                                                                                   |
| ----------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Project migration | Restore/evaluate every C# test project; preserve framework/platform targets and test discovery                                   |
| Compilation       | Debug test graph passes; affected rebuilds only. Repository owner validates Release                                              |
| Ordinary suites   | Repository owner runs full suites; agent runs only cases affected by fixes. Preserve failure and timeout exit codes              |
| UI suites         | Target failing cases/data rows; verify fixture behavior, realized content/resources and process shutdown                         |
| IDE               | Test Explorer discovery and execution; selected tests and debugger behavior on representative suites                             |
| Host lifecycle    | Discovery/dispatcher-only execution: zero visible windows; realized UI: one reused window; failure/timeout: no remaining process |
| Closure           | Separate and resolve test-case defects, review warnings and update concise status before commits                                 |

Pilot evidence: DynamicTree passes 51/51 in Debug and Release and 51/51 in
Visual Studio Insiders 18.11 Test Explorer. Five selected WorldEditor interaction
and host-lifecycle checks pass in Debug. Discovery and dispatcher-only execution
show zero windows; UI execution reuses one. Forced minimum-count failure and
timeout return nonzero status and leave no test processes. Editor IDE selection,
debugging and full-suite/Release qualification remain with the repository owner.

The two upstream-documented MSTest localization `PRI263` warnings remain visible;
no localization data or analyzer checks are disabled.
