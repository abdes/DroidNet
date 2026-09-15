# M07B workspace publication integration

Date: 2026-09-15

The packaged Release tests in `InspectorControlTests.WorkspacePublication.cs`
exercise the production workspace publication adapter with the installed native
ImportTool and running engine. The initial trigger group passes **5/5**; the
final combined trigger, lifetime and recovery run passes **15/15**.

Each case starts with no cooked content, creates a material through
`MaterialDocumentService`, edits and saves its scalar colour, and discovers it
through the real project catalog and material picker while automatic cooking is
paused. The picker reports Needs cooking, no published output and no mounted
material. Resuming the queue produces and mounts the material; the picker then
reports Current and Mounted.

The material is assigned to two native scene nodes through document commands.
A second saved colour is cooked by one of five triggers:

| Trigger | Result |
| --- | --- |
| Automatic Save | Both native bindings update; consuming scene remains unsaved |
| Cook asset | Both bindings update and publication reports mounted |
| Cook folder | Both bindings update and publication reports mounted |
| Cook current scene | Both bindings update and publication reports mounted |
| Cook project | Both bindings update and publication reports mounted |

All cases preserve material keys, the scene object, scene revision, Undo count
and dirty state. The picker reports Current and Mounted after replacement.
Publication uses the same dispatcher-owned pause, validated mount preparation,
native binding refresh, catalog refresh and resume implementation as the
workspace; the test does not manually remount output.

The additional lifecycle and recovery cases pass **10/10**:

- Undo, clearing an assignment and removing geometry while Save cooking is
  paused remain effective after publication. Undo can then be redone using the
  newly cooked material.
- Closing the consuming scene and activating another while the cook waits
  preserves the new scene's bindings and history; the old nodes remain absent.
- Cancelling queued work preserves the prior native material until explicit retry.
- An externally changed material source fails cooking without replacing the
  published material; restoring the source and saving a new colour recovers.
- Repeating each of the four explicit scopes leaves the native content revision
  and all published file timestamps unchanged and reports reused products.

The combined run exposed a catalog-registration race: staging's synchronous
reader registration could fail during a short status-reader registration. All
asynchronous production registrations now wait for the gate. Verification uses
one existing lease for nested reads rather than registering again behind a
waiting publisher. Deterministic staging, registration and nested-reader tests
pass with the full ContentPipeline suite, **449/449**.

Evidence:

- `artifacts/TestResults/m07b-workspace-publication.trx`: initial five cases.
- `artifacts/TestResults/m07b-workspace-lifetimes.trx`: reproduced failure.
- `artifacts/TestResults/m07b-workspace-lifetimes-fixed.trx`: 13 passing cases.
- `artifacts/TestResults/m07b-workspace-recovery.trx`: two passing recovery cases.
- `artifacts/TestResults/m07b-registration-pipeline.trx`: 449 passing cases.
- `artifacts/TestResults/m07b-registration-final.trx`: 94 affected ownership,
  staging and publication cases after diagnostic cleanup.
- `artifacts/TestResults/m07b-workspace-publication-final.trx`: all 15 cases
  after diagnostic cleanup.

These checks establish native binding values and service integration. Existing
control tests and user walkthroughs cover visible picker interactions and
viewport appearance. Full import and project-closure journeys retain their
separate qualification gates.
