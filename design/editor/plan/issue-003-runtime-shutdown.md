# Issue #3: Deterministic Runtime Shutdown

Status: `implemented; automated tests and startup/close smoke validated`

Issue: [#3](https://github.com/abdes/DroidNet/issues/3)

Design owner: [runtime-integration.md](../lld/runtime-integration.md).

## Failure Reporting Contract

An exception is useful when a caller can change its next action. Explicit
shutdown must tell its caller whether teardown completed, so it can avoid
restarting over a live engine, report the failure, or deliberately retry cleanup.
An exception alone neither releases resources nor establishes that retry is safe.

Use one teardown implementation with two entry-point policies:

- `ShutdownAsync` attempts every safe cleanup step, records failures, establishes
  the resulting ownership state, and then reports accumulated failures to its
  caller. A lease failure must not abort unrelated cleanup.
- `DisposeAsync` invokes the same teardown implementation as a non-throwing
  fallback. It records cleanup failures through the runtime diagnostic/logging
  path without replacing an exception already being unwound or interrupting
  disposal of other services. This is the approved reporting policy.

Distinguish failures encountered during teardown from resources still owned at
the end. A failed surface unregister followed by successful destruction of its
owning native runtime is reportable, but does not by itself imply a live engine
remains. `NoEngine` requires confirmed release of all runtime ownership;
incomplete cleanup leaves `Faulted` and retains the references needed for cleanup.

Final disposal rejects new runtime work once requested. A failed cleanup must
not make subsequent cleanup calls inert by prematurely marking disposal complete
or destroying synchronization primitives they still require.

## Implementation Work

1. Separate normal-operation state validation from internal teardown
   preconditions. Coordinate lifecycle transitions and in-flight surface work so
   shutdown cannot miss a reservation or race a native registration. Repeated
   shutdown/disposal must safely join or serialize cleanup.
2. Handle `Ready`, `Running`, partial initialization, and faulted execution.
   Preserve the original initialization/startup failure while recording any
   secondary cleanup failures. Reinitialization may not overwrite retained
   resources from an earlier engine instance.
3. Release each lease independently while the loop can process removal requests.
   Handle a loop that has already exited or exits during release; do not wait
   indefinitely for a callback that can no longer execute. Preserve ownership of
   failed releases until their native owner is confirmed destroyed.
4. Request loop stop and await completion before releasing resources it uses.
   Verify the interop completion contract, including dispatcher-posted cleanup;
   the current loop task is completed before `OnEngineLoopExited` is dispatched.
   If loop termination cannot be established, retain its context and runner and
   report incomplete cleanup. Do not add forced termination or an arbitrary
   shutdown timeout as an incidental policy.
5. Clear reservations, document counts, and orphaned viewport IDs only when the
   corresponding native ownership has ended. Ensure a new engine instance does
   not inherit reservations or context from its predecessor.
6. Establish an explicit application shutdown caller while the UI dispatcher
   and diagnostics are available. Verify actual DI disposal behavior: the editor
   currently registers `EngineService` as a singleton, while bootstrap teardown
   uses synchronous disposal and has no explicit `ShutdownAsync` call. Merely
   throwing from `DisposeAsync` does not establish an observed shutdown path.
   The caller must catch and record shutdown failures, avoid unsafe restart, and
   preserve cleanup/log flushing for other services. Coordinate this with the
   existing unsaved-document close transaction; cancellation must not stop the
   engine. Any proposed retry/exit dialog or change to window-close behavior
   requires a separate product decision before implementation.
7. Update lifecycle/interface documentation to the implemented contract and
   record only verified results in the implementation status document.

## Native Stop/Reset Race Found During Validation

The native regression test reproduced an access violation when `StopEngine` is
called after loop exit. `EditorInterface/EngineRunner.cpp` dereferences
`ctx->engine` unconditionally, while `RunEngine` resets that shared pointer.
Checking only the managed loop task does not cover the interval between native
teardown and managed task completion. This also makes concurrent stop/reset
unsafe. Fix the public engine API, rather than adding an editor-side workaround:

- Synchronize native stop with engine-owner removal, and make stop harmless when
  no engine remains.
- Destroy the removed engine outside the ownership lock.
- Make the engine stop flag atomic: the UI thread writes it while the loop reads it.
- Use the same ownership protection for the adjacent runtime FPS/config accessors.
- Keep the native regression's repeated stop after loop exit as an explicit test.

The engine owner rebuilt and reinstalled Debug after the source correction.
The repeated-stop/native cleanup regression then passed against the installed
engine. Engine builds remain owned by the user unless explicitly authorized.

Application startup validation also exposed eager window-manager resolution from
hosted-service construction. Hosted services are constructed before UI startup,
and starting the UI thread does not synchronously populate the dispatcher.
`EngineShutdownService` therefore has no window-manager constructor dependency.
The application connects `ObserveWindows` after the dispatcher exists, using the
same singleton exposed through `IHostedService`. The regression tests reproduce
DryIoc hosted-service resolution with an unavailable dispatcher and a window
factory that rejects premature construction.

## Tests And Verification

Add a narrow native-lifetime dependency seam for direct `EngineService` tests;
do not substitute tests of a separate shutdown algorithm for service tests.

Cover:

- Shutdown from `Ready` and `Running`, with zero and multiple leases.
- Partial initialization and synchronous/asynchronous loop failures.
- One lease failing while remaining resources are still cleaned up.
- Stop/context/runner cleanup failures, retained ownership, and explicit retry.
- Loop exit during lease release and ordering of context destruction.
- Repeated/concurrent shutdown and disposal, plus restart without stale state.
- Explicit shutdown reporting failures versus fallback disposal recording them.
- The application shutdown caller observing failure and running before required
  dispatcher teardown; canceled document close leaves the engine running.

Build the editor with `MSBuild.exe` using the repository's existing output paths
and installed engine binaries. Check compiler, analyzer, and IDE diagnostics in
every changed C# file, including informational results.
Report automated validation separately from manual editor shutdown validation.

## Verified Results

- The engine owner rebuilt and reinstalled the affected Debug engine binaries.
- Standard Debug editor MSBuild succeeds using the existing output directories.
- 29 runtime tests pass, including native repeated stop, dispatcher cleanup,
  direct service failure injection, DryIoc startup resolution, host shutdown
  ordering, and document-close cancellation/finalization.
- 13 existing Aura window-close tests pass.
- All 3 native EditorInterface linked tests pass, including empty-context stop.
- Compiler/analyzer/IDE SARIF reports contain no active diagnostics in modified
  C# source or test files, including the application startup integration.
- An editor launch reached a responding native window and exited through a
  normal window close request. Full manual dirty-document/viewport scenario
  replay was not performed in this change.
