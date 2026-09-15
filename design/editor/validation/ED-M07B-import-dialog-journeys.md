# M07B import dialogs, replacement and retry

Date: 2026-09-15

Four packaged integration cases exercise glTF and FBX through the actual Aura
dialog service, import review controls, Content Browser import entry point,
native pipeline, Cooking panel and running engine. The final related native
import/typed-use/publication/lifetime run passes **25/25**.

For each format, the import/replacement case verifies:

- Invalid names disable the dialog's primary action and show inline validation.
- Cancel creates no cooking run or retained source.
- Accepting the reviewed name/destination retains source, cooks three named
  outputs and exposes the run's messages and outputs in Cooking.
- A name collision requires explicit replacement review. The actual primary
  button changes to Replace and import.
- Cancelling replacement preserves retained source bytes and all published
  hashes. Accepting replaces source and output under their existing identities.
- The new scalar material values reach an already assigned native scene node
  without adding scene history or clearing its unsaved state.

The retry case injects one failed process result at the importer boundary after
real source discovery and retention. Cooking displays the failure and its output.
The original external file is then removed. Clicking the actual Retry button
uses retained source/settings, runs the real native cooker and produces a
material that can be assigned to the running scene.

The window-registry test double supplies the real test window to Aura; the
dialog itself, validation, button events and native pipeline are production
implementations. Button selection matches both template-part name and displayed
label, avoiding the review InfoBar's hidden Close button. Cleanup awaits modal
closure before unloading the owner.

Evidence: `artifacts/TestResults/m07b-native-import-journeys-final.trx`.
The failure/result Cooking captures under
`artifacts/TestResults/abdes_GIGA_2026-09-15_06_47_06/In/` were reviewed.
All changed files have no analyzer or IDE diagnostics.
