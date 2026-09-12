# Cooking Panel

Status: `layout and interactions approved 2026-09-12; implementation underway`

## 1. Purpose And Scope

Provide one place to follow and recover cooking work for an asset, scene, folder,
or project. Explicit Cook actions reveal this dockable panel. Automatic cooking
uses the same presentation without opening it or taking focus. Asset rows and
property slots retain concise status and a link to the relevant run.

Reuse the workspace docking system, ContentPipeline coordinator, ordinary
document saves, shared operation results, and existing logging infrastructure. The panel
does not create another scheduler, native-worker owner, or publication path.
Neither a scene-toolbar error banner nor a modal progress dialog is the primary
cook recovery surface. A general notification framework is not required here.

This contract belongs to ED-M07B.5d-f and complements
[content-cooking-workflows.md](content-cooking-workflows.md),
[content-pipeline.md](content-pipeline.md) sections 16-19, and
[diagnostics-operation-results.md](diagnostics-operation-results.md).

## 2. Accepted Interaction Decisions

| Topic | Accepted behavior |
| --- | --- |
| Default docking | Bottom, tabbed alongside Content Browser and Logs; rearrange through existing docking. |
| Organization | Compact list of active, queued, and recent cooks on the left, using status icons beside names; details for one selected cook on the right. |
| Issues | Directly beneath the selected cook's progress summary, grouped by affected asset. Technical output has its own section within Cooking. |
| Output | Show the selected run's progress messages and technical output inside Cooking. Understanding a cook never requires switching to the global Logs panel. |
| Default detail | Current stage and asset counts; full asset list expandable. |
| Asset failure in a batch | Continue independent work, skip dependent work, collect all issues. A failed cook does not replace published output. |
| History | Retain this editor session's runs only. No completed history after restart. |
| Automatic runs | Never open the panel or steal focus. Show active, queued, failed, and warning runs by default; successful automatic runs appear through Show all. |
| Cancel | Cancel only the selected cook; other queued cooks continue. Show Cancelling until a safe stopping point. |
| Unsaved inputs | Put Save listed & Cook inline with Unsaved documents; each document name is its Open link. Saving requires explicit action. |
| Retry | Immediately resubmit the original scope with the latest saved inputs; reuse valid current outputs. |
| Go to property | Keep Cooking visible and the failed run selected while opening and focusing the affected property. |
| Visual treatment | Compact rows, clear grouping, restrained status colours, consistent with the editor. |

## 3. Layout

The review wireframe uses the existing dark editor styling, compact Segoe UI
typography, ordinary controls, fine dividers, and restrained selection accents.
Production uses existing WinUI theme resources, including light and high-contrast
themes, rather than duplicating the wireframe's colours.

### Dock And Toolbar

For new/reset layouts, Cooking joins the bottom Content Browser / Logs group.
Respect restored user layouts. Opening a previously hidden Cooking panel restores
its last docking location. A View/docking command makes it discoverable again;
hiding it never cancels work or clears history.

There is no panel-wide project-name toolbar and no Cook project command here.
Cooking displays work initiated from the existing editor cook entry points.
Cancellation and the accepted recovery actions remain beside their affected run.
Use DroidNet's compact ToolBar above the operations list for Show all. At narrow
widths, it stays with the run selector above the details. The D1 session Pause
automatic cooking / Resume control belongs in the existing Cook menu. Pausing
prevents automatic work from starting; it does not cancel active work or block
explicit cooks.
The Cooking tab can show active/attention status without activating itself.
Historical failures are not an ever-growing count of current project problems.

Initial bottom height is about 340 DIP, with a resizable left list about
240 DIP wide. Use the editor's existing spacing resources.

### Operations List

Use two-line rows: status icon beside scope name, then scope kind and time.
Use the same status-icon vocabulary as the asset list. Provide distinct shapes,
accessible state labels, and tooltips; colour reinforces the icon. Do not repeat
the status as a distant text badge. Omit the routine Explicit trigger label;
identify Automatic only when that distinction is useful. The selected run's
details carry the status explanation and actions.
Group active/pending work above recent outcomes. Needs save is a pending request,
not a worker holding the execution slot. Long names remain distinguishable;
paths, hashes, stack traces, and diagnostic paragraphs do not belong in rows.

Selection rules:

- Explicit submission reveals and selects its new or joined run. Background
  events never move selection, keyboard focus, or the current reading position.
- A selected automatic run remains visible when it succeeds, even with Show all
  off, until selection moves. A filtering update must not remove the details
  the user is reading.
- Show all reveals successful automatic runs as well as otherwise hidden
  terminal runs. Failed, warning, and blocked automatic work remains actionable
  in the default view. Cancelled automatic runs are available through Show all.
- Display the active project's history; retain other projects' summaries during
  the session so switching back does not lose context. Project close still
  cancels/drains owned work through the existing lifecycle contract.

### Selected Run

Keep the following order:

1. One compact header: Main (Scene), a status pill such as Failed, then the
   applicable Retry/Cancel action inline. Use WinUI semantic status colours for
   the pill and list icons, including the critical colour for failures. Cancel
   applies to pending/active work, Retry to failed/cancelled work, and
   Save listed & Cook to blocked unsaved inputs.
   Completed and already-current cooks have no Retry button, including no
   disabled placeholder.
2. While running, show useful progress beneath the header. Do not repeat the
   status as a separate paragraph.
3. Native Expanders explicitly label asset issue groups and counts, such as
   Issues · Main (1). Native InfoBars carry severity, the short issue message,
   and an inline recovery action, wrapping automatically when space requires it.
4. Output, expanded by default, with this run's chronological progress messages
   and technical output. It remains available after completion.
5. Expandable Assets list with each asset's name, type, and adjacent status icon.
   Keep these together; do not place status in a distant right-aligned column.
   Use distinct status icons, accessible names, and concise tooltips for Updated,
   Reused, Failed, Skipped, and pending states. Show dependency-skip reasons when
   inspecting the affected row. List actual assets rather than type-only count
   rows that conceal their identities.

### Output Within Cooking

Selecting a run selects its transcript automatically. Include managed preparation,
native cooker messages, validation, publication, rollback, and cancellation in
one ordered stream. Show time, severity, and affected asset where available.
Unrelated editor messages never enter this view. The global Logs panel remains
available for broader editor diagnostics; no View log navigation is needed to
understand the selected cook.

Keep Output visually distinct from the actionable issue groups above it. Output
explains the sequence of events; issues expose the property/asset recovery actions.
Use compact selectable message rows, wrapping long text and preserving native
detail. Do not replace the transcript with only a generic success/failure summary.

The selected run's body scrolls while its compact header remains visible. Output
and Assets use native Expanders that grow to fit their complete content. Neither
section owns a scrollbar; the details pane owns all content scrolling.
Expanded Output has a minimum content height of 160 DIP; expanded Assets has a
minimum of 120 DIP. A short dock scrolls to these sections instead of clipping
them. Output is initially open. Follow new messages while the user is at the end; scrolling
back pauses following and offers Jump to latest. Preserve that reading position,
expanded state, and selected text across incoming messages and run switches.
Large transcripts remain accessible through that single scrolling surface.

Use Preparing, Cooking, Validating, and Updating preview as truthful stages.
Discovery may be indeterminate. Counts reflect known work; elapsed time never
becomes a fabricated percentage. A reused product is not counted as newly cooked.
All-current inputs finish as Already up to date without invoking native workers.
Do not stack Succeeded, updated counts, and Published / Preview updated as three
success statements. Counts belong in Assets or Output after completion. Show an
additional status only when it changes the user's next action or expectation,
such as Preview unavailable or Completed with warnings. Publication steps and
routine technical confirmations belong in Output.

An error must identify the affected asset and, when available, node and property.
Show the value captured by the failed run separately from the current editor
value. Messages wrap; actions remain available without hover. A project-wide
problem such as tool mismatch sits in its own scope group, not under a guessed
asset. Success with warnings stays visible and states whether output published.

Report the actual publication outcome: previous output retained, previous output
restored, or recovery required. Cooked / preview unavailable is distinct from
cook failure. Successful independent staging work remains inspectable when a
batch fails, but the failed batch publishes none of those changes.

## 4. Interaction And State Rules

### Submit, Retry, And Concurrent Work

Retain original logical scope and source identity with each run. Retry resolves
that scope against the latest saved inputs; it does not replay a historical
snapshot or widen an asset request to a project request. Recheck removed sources,
changed dependencies, dirty documents, and project lifetime before execution.

Use existing request coalescing, saved-input capture, priority, and cancellation
ownership. An equivalent active automatic request can serve an explicit Retry;
select the joined run and identify the shared request rather than cooking twice.
A newer saved revision creates/coalesces pending work without changing the
running snapshot. No queue reordering UI or second priority policy is introduced.

### Go To Property

Navigate using stable project/document/node/property identity. Open the document,
select the target, expand the appropriate section, scroll it into view, and focus
the editor. Preserve unrelated unsaved edits and the selected Cooking run.
Ensure the Inspector and Cooking remain visible, including layouts where the
Inspector previously shared its tab group. This is the explicit navigation
action's effect; background issue arrival never rearranges docks.

If the asset or target no longer exists, keep the historical issue and explain
why navigation is unavailable. Do not navigate to another node with a similar
name. Fixing a source does not rewrite a failed run's captured values or outcome.
The next result reports the repair; current asset status follows publication.

### Needs Save

List exactly the unsaved documents participating in the request as document-name
links. Put Save listed & Cook inline with the Unsaved documents heading. The
action invokes their ordinary Save commands and existing
conflict handling. It does not save unrelated documents or overwrite conflicting
external changes. Recheck the dependency closure after saving; newly discovered
dirty inputs require an updated visible list and another explicit save action.

If a save fails, is cancelled, or a document changes again before capture, the
request remains blocked with the reason. Successful Save remains successful even
when the subsequent cook fails. A save-triggered automatic cook and this explicit
request share equivalent work; no duplicate publication occurs.

### Cancel And Queue

Cancel removes only the selected pending request or asks its active operation to
stop. Show Cancelling while workers and descendants drain. At the publication
boundary, show Finishing update until commit/rollback establishes the actual
outcome; do not report cancellation after a successful commit. Then admit other
eligible queued requests normally. Cancelling does not disable automatic cooking
globally or create an automatic retry loop for the same revision.

### Session History

Keep summaries, diagnostic references, and each run's ordered messages available
for this editor session. Retain meaningful phase/messages rather than every
progress tick. Reuse existing capture/storage capabilities, with a run-scoped
transcript whose availability does not depend on global log filtering or unrelated
editor traffic. Hiding the panel preserves state, while restarting does not
restore completed history. Durable publication recovery records have a
separate lifetime and must still recover an interrupted update after restart.

## 5. Density And Accessibility

Keep one information hierarchy across both lists: status icon, primary name,
secondary type/context. Use consistent icon meaning for active, queued, blocked,
failed, cancelled, completed, and reused states. Put detail where it is needed:
one selected-run status, actionable issue text, then that run's Output and Assets.
Avoid repeated success statements, redundant routine labels, and inactive actions.

Keep the normal view focused on the selected run. Its Output section shows only
that cook's messages; the full asset list stays expandable. Virtualize long
operation, message, and asset lists. At narrow dock widths, replace the left column with
a compact run selector above details; preserve all issue actions. The contained
review preview stacks its list and details at narrow conversation widths.

Keyboard users can select a run, expand assets, invoke recovery, and reach the
property editor. Announce phase/outcome changes without reading every progress
tick. State uses text and appropriate existing icons as well as colour. Preserve
focus across updates. Verify long names/messages, 100%/150%/200% scaling, high
contrast, a narrowed dock, and a 1,000-asset project without clipped controls.

## 6. First Acceptance Journey: Main

The reported Main scene has a saved Aerial Start of -1 m, while the native scene
schema requires a finite value of at least 0 m. Its cook fails and retains the
previous output; its stale state must not be cleared by that failure.

1. Load the saved invalid value visibly. Do not silently clamp it or mutate the
   document during load.
2. Cook Main. Show its failure in Cooking with Aerial Start, the minimum 0 m,
   and the captured -1 m. State that previous output is retained. Read the full
   preparation/validation failure sequence directly in this run's Output section.
3. Go to property keeps this run selected while focusing Aerial Start. Apply
   the native schema's bound through shared validation in the property edit
   pipeline and cooking preflight. Invalid negative/nonfinite edits are rejected
   without a history entry; 0 and 100 are valid. Show inline field feedback.
4. Enter 100. Retry before saving becomes Needs save and lists Main.
5. Save listed & Cook, or ordinary Save followed by Retry, captures the latest
   saved value. Reuse/coalesce equivalent work from D1 automatic cooking.
6. Successful validated publication updates Main's current status and output.
   Reopening preserves 100; the older failure still records -1. Verify in the
   editor that Main is no longer stale when its saved inputs are current.

Additional gates cover every scope; independent asset failures and dependent
skips; warnings; first-cook failure; cancellation during queue/work/publication;
automatic visibility and stable selection; dirty dependencies, save conflicts,
and newer edits; removed navigation targets; hide/reopen and project switching;
no-op retries; offline preview and recovery failure; keyboard/scaling/density;
run-scoped live/completed messages, unrelated global-log traffic, long transcripts,
and stable reading position while messages arrive.
The review wireframe simulates these interactions. Implementation and actual
editor validation are separate acceptance steps.

## 7. Implementation Ownership

| Owner | Responsibility |
| --- | --- |
| ContentPipeline coordinator | Cook-specific observable run state, scoped progress, request ownership, capture, cancellation, retries, provenance, and publication. |
| WorldEditor workspace | Dock registration, run-list/details/output presentation, stable selection and reading position, and property/document navigation. |
| Document and property pipelines | Ordinary save/conflict commands and shared schema-derived bounds; preserve existing edit/history behavior. |
| Shared operation results and logging infrastructure | Immutable finalized outcomes, actionable diagnostic identity, and run-correlated messages available directly in Cooking. Reuse capture/storage without requiring the global Logs view. |
| Content Browser and typed pickers | Concise current state from the same publication authority and navigation to the relevant Cooking run. |

Run presentation consumes snapshots/events from the owning coordinator. It does
not infer success from filesystem timestamps or parse free-form log text as the
only source of an actionable property error. Views never control native
processes, move cooked files, or bypass publication validation.
