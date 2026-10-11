# ED-M09 - Input Authoring

Status: `planned; design drafted, awaiting owner review (M09.1)`

| Outcome                                                                                                                                                                                                     | Remaining                                                                                      | Evidence                                                                              |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| Users create and edit input actions and mapping contexts in the editor, bind keys and mouse input by pressing them, configure triggers, rename with retargeting, save, cook and use them in Play (REQ-045). | M09.1 owner review of the [input authoring LLD](../lld/input-authoring.md), then M09.2–M09.10. | None yet; the LLD's [gates](../lld/input-authoring.md#14-validation-gates) define it. |

The [input authoring LLD](../lld/input-authoring.md) owns the workflow,
format, editors, commands, diagnostics, gates and open questions. This plan
owns order, dependencies and acceptance.

## Starting point

- The engine imports, cooks and runs input actions and mapping contexts; its
  formats are the authoring format.
- ED-M08.10o cooks hand-written input files under authoring mounts and lists
  their assets in pickers; ED-M08.10p–q add script parameters and the scene's
  Play section that reference them
  ([script authoring](../lld/script-authoring.md)).

## Slices

| Slice  | Delivers                                                                                                                                                                            | Projects                                                                 | Depends on                 |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ | -------------------------- |
| M09.1  | Owner review of the LLD's user interface (sections 9.1–9.5) and decisions on IQ-1–IQ-12; LLD and this plan corrected; touch points (IQ-10) amended in their owning LLDs.            | Design documents                                                         | —                          |
| M09.2  | Engine gap closure the owner schedules for V0.1. Recommended: scale and bias applied at runtime (EG-1) and schema rules (EG-2). Default if declined: none.                          | Oxygen.Engine (Input, Content, Cooker schemas), with its own design note | M09.1                      |
| M09.3  | Input source model, schema-validated reader and canonical writer, editor schema overlay, project input index; Content Browser rows per declared asset, tiles, details, Broken rows. | Managed.Assets, Editor.Schemas, ContentBrowser                           | M09.1, ED-M08.10o          |
| M09.4  | Cooking: name-based `depends_on`, context roots, multi-output provenance and retirement of outputs no longer emitted, cooker diagnostics mapped to properties.                      | ContentPipeline                                                          | M09.3                      |
| M09.5  | Input document service (history, save, conflicts, relocation participant), creation commands, action editor.                                                                        | InputEditor (new), Documents, ContentBrowser, app composition            | M09.3                      |
| M09.6  | Context editor: mapping list in evaluation order with action groups, action picker, key selector, key capture session, shared key translation, multi-selection, clipboard.          | InputEditor, Editor.Runtime, WorldEditor                                 | M09.5                      |
| M09.7  | Trigger editing: cards for every type, behaviors, chord action, combo steps with flags and windows; scale and bias fields if M09.2 delivered EG-1.                                  | InputEditor                                                              | M09.6, M09.2               |
| M09.8  | Rename, move, delete and Split with retargeting across input files, Play sections and script overrides.                                                                             | ContentBrowser, ContentPipeline (relocation), InputEditor                | M09.5, ED-M08.10p–q        |
| M09.9  | Scripting interop: New action and New context from script and Play-section pickers; Play priority default from source; Edit opens the input editor.                                 | WorldEditor, ContentBrowser                                              | M09.6, ED-M08.10q          |
| M09.10 | Closeout: owner review in the running editor and a Play run on editor-authored input.                                                                                               | All above                                                                | M09.4, M09.7, M09.8, M09.9 |

## Acceptance

Each slice is complete when its checks pass and the LLD matches what was
built.

| Slice  | Acceptance                                                                                                                                                                                                              |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M09.1  | Every IQ row has a recorded decision; the LLD reflects it; no open question blocks M09.3.                                                                                                                               |
| M09.2  | Per the engine design note; for EG-1, a native test shows W with scale (0,1) and S with scale (0,−1) on one Axis 2D action produce (0,±1); for EG-2, schema tests reject each rule the LLD lists.                       |
| M09.3  | IA-1 and IA-2: the three example input files open, save unchanged in value and cook to identical descriptors; a multi-asset file shows one row per asset; a schema-invalid file shows one Broken row with its messages. |
| M09.4  | IA-7: saving an action file recooks the contexts that use it; a context in another mount resolves the action; renaming an action removes the old key from the index; a cook error's Go To Property selects the field.   |
| M09.5  | IA-6 for the action editor; New ▸ Input creates, selects and opens each kind; an external edit of an open file offers Reload and Save copy.                                                                             |
| M09.6  | IA-3 key capture tests pass headlessly; a user binds Space by pressing it and Mouse XY from the list; reordering changes the written order; Group by action warns before changing order.                                |
| M09.7  | IA-4: each trigger type's fields edit, validate, undo and round-trip; a combo of two actions with a zero window on step 2 shows `OXE.INPUT.COMBO_STEP_UNREACHABLE` and blocks the cook.                                 |
| M09.8  | IA-5: renaming an action used by two contexts, a Play section and a script override updates all four files in one transaction; Undo restores them; Split keeps every identity.                                          |
| M09.9  | A script parameter of kind input action can create a new action from its picker; the Play section shows the context's own priority until overridden.                                                                    |
| M09.10 | IA-8 and IA-9: a scene starting an editor-authored context runs in the player and a script's `on_action` fires; the owner accepts SC1–SC10 in the running editor.                                                       |

## Open items

Details live in the LLD's [open questions](../lld/input-authoring.md#152-open-questions)
and [engine gaps](../lld/input-authoring.md#151-engine-gaps).

| ID    | Priority | Item                                                                | State | Next action            | Owner |
| ----- | -------- | ------------------------------------------------------------------- | ----- | ---------------------- | ----- |
| IQ-1  | 1        | Authoring format and file granularity                               | Open  | Decide at M09.1        | Owner |
| IQ-2  | 2        | Scale and bias have no runtime effect (EG-1)                        | Open  | Decide at M09.1        | Owner |
| IQ-3  | 3        | Input asset identity and name scope                                 | Open  | Decide at M09.1        | Owner |
| IQ-12 | 4        | Schema rules the runtime depends on (EG-2)                          | Open  | Decide at M09.1        | Owner |
| IQ-10 | 5        | Touch points in script authoring, content browser, content pipeline | Open  | Amend after M09.1      | Owner |
| IQ-4  | 6        | Name pattern                                                        | Open  | Decide at M09.1        | Owner |
| IQ-11 | 7        | Mapping order presentation                                          | Open  | Decide at M09.1        | Owner |
| IQ-5  | 8        | Chord and action chain as one type                                  | Open  | Decide at M09.1        | Owner |
| IQ-6  | 9        | Load and Activate flags in the context editor                       | Open  | Decide at M09.1        | Owner |
| IQ-8  | 10       | `Oxygen.Editor.InputEditor` module                                  | Open  | Decide at M09.1        | Owner |
| IQ-9  | 11       | `Content/Input` default folder                                      | Open  | Decide at M09.1        | Owner |
| IQ-7  | 12       | In-editor live preview                                              | Open  | Recommended: post-V0.1 | Owner |

## Note on the ID

ED-M09 previously named the viewport authoring tools, which ED-M08.4–M08.6
delivered on 2026-10-08.
