# M07B Save listed and Cook recovery

Date: 2026-09-15

The packaged integration case starts with a published material used by two scene
nodes. Both that material and an unrelated material receive unsaved edits.
Cooking the shared material becomes Needs save and lists only its document.

An external write changes the shared material's saved file. Clicking the actual
Save listed & Cook button routes the named document to `MaterialDocumentService`.
The atomic save reports a conflict; the source and published hashes remain
unchanged, the edit stays dirty, and the cook remains blocked with recovery
feedback in Cooking.

An explicit document reload accepts the external value. Clicking Save listed &
Cook again resumes the original operation, clears the action error and publishes
the new value to the native scene. The unrelated material and consuming scene
remain unsaved. The workspace-action test double only routes the displayed
document list to the real material save service; it asserts the exact document
identity on both calls.

The test passes alone and in the final **7/7** import/output/project-lifetime
group against the rebuilt native SDK. Changed files have no analyzer or IDE
diagnostics.

Evidence: `artifacts/TestResults/m07b-plain-output-save-recovery.trx`.
