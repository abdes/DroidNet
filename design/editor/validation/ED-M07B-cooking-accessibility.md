# M07B Cooking automation names

Date: 2026-09-15

The automation check reproduced a Cooking operation row announcing
`Oxygen.Editor.World.Cooking.CookingRunViewModel` instead of its displayed
identity. Row containers now bind their automation name to the data template's
current item. Operations expose name, scope/origin and status; assets expose
name, type and status. Bindings follow item and status changes.

The test reads the actual ListViewItem automation peers for an operation and an
asset, then changes their status and verifies the updated names. It passes with
the related Cooking/Main control group, **22/22**. Existing standard-control,
keyboard and scale evidence remains recorded separately.

Evidence: `artifacts/TestResults/m07b-cooking-automation.trx` records the failing
name; `artifacts/TestResults/m07b-main-automation-final.trx` records the fix and
related controls. Changed files have no analyzer or IDE diagnostics.
