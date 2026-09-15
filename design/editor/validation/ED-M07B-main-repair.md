# M07B Main scene repair

Date: 2026-09-15

The combined packaged test begins with a valid Main scene published at Aerial
Start 0, then saves the legacy invalid value -1 and displays it in the actual
environment inspector. Cooking fails with the minimum 0 m and captured -1 in
its diagnostic/output; every previously published file retains its hash.

The actual Go to property link focuses Aerial Start while the failed run stays
selected. Entering -2 is rejected without history; entering 100 creates the
authored edit. Retry becomes Needs save and lists Main. Save listed & Cook uses
the real scene save/read gates and resumes the request. Native publication
succeeds and the shared status reader reports Current.

Save/reopen preserves source and native Aerial Start 100. The earlier failure
still records -1. The workspace-action test double routes the diagnostic to
the real environment control and the named document to its real command service;
the pipeline, publication adapter, catalog and engine are production services.

The final related Cooking/Aerial/control run passes **22/22**, with no analyzer
or IDE diagnostics in changed files. The reviewed capture shows Aerial Start
100 and the completed cook beside the retained failed run.

Evidence: `artifacts/TestResults/m07b-main-automation-final.trx`.
Reviewed capture:
`artifacts/TestResults/abdes_GIGA_2026-09-15_07_49_30/In/6c427008-485d-4861-b6ef-4fdcfe7f805f/GIGA/main-cook-repair.png`.
