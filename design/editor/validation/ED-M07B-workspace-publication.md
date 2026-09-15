# M07B workspace publication integration

Date: 2026-09-15

The packaged Release tests in `InspectorControlTests.WorkspacePublication.cs`
exercise the production workspace publication adapter with the installed native
ImportTool and running engine. The focused run passes **5/5**.

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

Evidence: `artifacts/TestResults/m07b-workspace-publication.trx`.
These checks establish native binding values and service integration. Existing
control tests and user walkthroughs cover visible picker interactions and
viewport appearance. Interruption, cancellation and import journeys retain
their separate qualification gates.
