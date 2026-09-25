# EX08.1 console controls

Status: validated and closed (2026-09-25), structured commit delivery authorized after user review.
EX08.1 is a prerequisite of EX10.

Use the existing console registry, policies, help/completion and automation source.
Use existing post-process/camera settings owners and renderer-issued transition
tokens. No new experiment controller, persistence store or runtime measurement.

1. **Command lifetime:** add handle-based unregistration. Obsolete handles must
   not remove a replacement registration. Completion must discard retired names.
   Execution pins the registration, so a callback may unregister itself without
   destroying its active callable or losing mutable callable state.
2. **Post-process commands:** inspect/edit complete validated settings through the
   same owner as the panel. Explicit target identity/revision, atomic invalid-input
   rejection, camera inputs, mask/curve and output settings remain required.
3. **Transitions:** enumerate the renderer's published persistent exposure owners;
   seed/remeter use existing public APIs. Report queued/applied/rejected/superseded
   token state rather than claiming immediate GPU application. Reject stale targets.
4. **LightBench:** register local preset/reset commands directly against its
   existing operations; unregister on shutdown. No adapter/controller platform.
5. **Qualification:** native tests cover validation, lifetime, automation access,
   stale targets and transition ordering. Build Debug and Release in the existing
   Ninja tree. The user checks integrated console interaction before closure.

Command lifetime and the post-process/LightBench bindings are now implemented.
Debug and Release each pass 50 Console checks, seven post-process console checks,
seven existing settings-owner checks and two local LightBench command checks.
The [operating guide](../../../../../Examples/DemoShell/Console.md) specifies the exact
syntax, target scopes and acceptance meanings. The final source checkpoint was
rebuilt and checked on 2026-09-25 in the existing non-Tracy Ninja tree.

| Native suite                                                       |     Debug |   Release |
| ------------------------------------------------------------------ | --------: | --------: |
| Console registry, policies, completion and command lifetime        |     50/50 |     50/50 |
| Post-process command validation, targeting and transition ordering |       7/7 |       7/7 |
| Existing post-process settings owner                               |       7/7 |       7/7 |
| LightBench preset/reset command lifetime                           |       2/2 |       2/2 |
| **Total**                                                          | **66/66** | **66/66** |

[Durable results](evidence/validation/ex081-20260925/summary.json) include raw result/log
archives, build logs and source hashes. The new binding translation units are
clang-tidy clean across three Debug compile contexts. The modified existing
owners have zero diagnostics on changed lines; 127 other diagnostics remain
in those files, so this is not a whole-file clean claim. `git diff --check` passes.
Existing render qualification remains applicable; no renderer benchmark or
capture was repeated for these console changes.

The real console input widget now passes edit, atomic rejection, stale target,
preset application and reset checks in Debug/Release. The user also accepted
ordinary MultiView exposure edits and console target discovery. The
[EX08.2 migration record](../EX08.2/validation.md) preserves the renamed/simplified
LightBench CPU checks and real-app results. Native renderer tests remain the
authority for transition consumption/history; no new GPU capture was run.
The operating checklist remains available for future changes. The user
subsequently authorized structured commit delivery after review.

Review the changes in this order:

1. `src/Oxygen/Console/{Console,Registry}`: registration handles and safe callback
   lifetime when a command retires itself.
2. `Examples/DemoShell/Services/PostProcessSettingsService`: atomic camera/output
   edits and discovery of the existing scene owner.
3. `Examples/DemoShell/Internal/PostProcessConsoleBindings`: command grammar,
   complete validation, stale targets and truthful asynchronous status.
4. `Examples/LightBench/LightBenchConsoleBindings` and `MainModule`: direct
   preset/reset integration and shutdown cleanup.
5. The corresponding Console, DemoShell and LightBench tests.

The user subsequently authorized structured commits after implementation,
qualification and review.
