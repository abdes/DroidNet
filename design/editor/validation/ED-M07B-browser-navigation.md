# M07B browser navigation and command scope

Date: 2026-09-15

The final packaged Release run passes **36/36** browser UI cases. The Content
Browser suite passes **134/134**. Changed files have no analyzer or IDE diagnostics.

The new combined journey renders the complete browser view with its real local
router, folder tree, list/tile views, breadcrumbs and query controls. A controlled
catalog supplies 1,000 logical rows across Materials, Geometry and Scenes.
Cook requests are captured at the pipeline boundary to verify their exact scope;
native cooking itself is covered by the separate publication and typed-use runs.

Both light and dark cases verify:

- Materials and Scenes filters return 667 entries; search narrows these to 67.
  Clearing the query restores 1,000 entries without submitting cooking work.
- Folder navigation and layout changes keep rows, tree selection and breadcrumbs
  aligned. Four Back and four Forward steps restore each folder/layout pair.
- At every tested position, Cook Asset receives the selected row's URI and Cook
  Folder receives the visible folder's URI.
- Navigating after Back replaces the forward branch. A later catalog snapshot
  cannot repopulate a different folder or change the command scope.

The first run reproduced silent failure of programmatic folder navigation:
`FindFolderAdapterAsync` searched ordinary folder children beneath a project
whose children are mount adapters. Navigation now resolves the physical folder
through declared mount roots, updates the canonical browser scope and uses the
existing selection path to reveal ancestors. Focused cases cover a renamed
authoring mount, a renamed Cooked mount and an external cooked library.

Three older cooked-folder cases were corrected to declare their cooked mount.
The editor distinguishes a mounted derived root from an ordinary folder named
Cooked; the fixtures now follow that contract.

Full-size light/dark captures were reviewed at 100% XAML scale. They show the
selected Materials folder, matching breadcrumb, list rows, navigation controls
and command toolbar. Separate scaled-control and real-catalog workload evidence
retains its existing scope.

Evidence:

- `artifacts/TestResults/m07b-browser-history.trx`: reproduced navigation failure.
- `artifacts/TestResults/m07b-browser-history-qualified.trx`: 36 passing UI cases.
- `artifacts/TestResults/m07b-browser-history-unit.trx`: 134 passing browser cases.
- `artifacts/TestResults/abdes_GIGA_2026-09-15_05_51_55/In/`: reviewed full-size
  `browser-history-False.png` and `browser-history-True.png` captures.
- `artifacts/m07b-browser-history-qualified-build.log`: final test build.
