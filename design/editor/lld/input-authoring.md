# Input Authoring LLD

Status: `design draft; awaiting owner review (ED-M09.1)`; delivered by
[ED-M09](../plan/ED-M09-input-authoring.md).

Summary: users create input actions and input mapping contexts from the
Content Browser and edit them in two focused editors. A context lists its
mappings in evaluation order, grouped by action; each mapping binds one
keyboard or mouse input, captured by pressing it or picked from a list, and
carries an ordered list of triggers edited as typed cards. The files are the
engine's own `*.input-action.json` and `*.input.json` formats, so
hand-written files open in the same editors. Save cooks; renames retarget
every input file, scene and script that refers to the asset; Play runs the
result. The engine owns the input model, the formats and the cooker
([input cooking architecture](../../../projects/Oxygen.Engine/design/content-pipeline/input-cooking-architecture.md));
this LLD owns the authoring workflow. Read [scenarios](#1-purpose),
[format](#7-data-contracts), [editors](#9-ui-surfaces),
[engine gaps](#151-engine-gaps) and [open questions](#152-open-questions).

## 1. Purpose

Users author the input actions and mapping contexts their scenes and scripts
use, without writing JSON. Scenarios:

| ID   | Scenario                                                                                                                                     |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| SC1  | Create an input action from the Content Browser; choose its value type and whether it consumes input.                                        |
| SC2  | Create an input mapping context; set its priority.                                                                                           |
| SC3  | Add an action to a context and bind keys and mouse input by pressing them or picking them from a list.                                       |
| SC4  | Add, reorder, configure and remove triggers on a mapping, including hold, tap, pulse, chord and combo.                                       |
| SC5  | Rename, move or delete an action or context; every input file, scene Play section and script parameter that refers to it follows or reports. |
| SC6  | Open an existing hand-written input file, including one that declares several actions and contexts, and edit it in place.                    |
| SC7  | Undo and redo every edit; the document shows unsaved changes until saved.                                                                    |
| SC8  | Save; the file cooks; errors appear on the field that caused them.                                                                           |
| SC9  | Pick the action in a script parameter and the context in the scene's Play section; press Play; the script receives the action.               |
| SC10 | See an asset's users (contexts, scenes, scripts) before changing or deleting it.                                                             |

## 2. PRD Traceability

REQ-045 (this design); REQ-043 and REQ-044 consume its assets (script
parameters, Play section). REQ-006/007 (undo, dirty, round trip), REQ-021/022
(shared asset state, visible failures), REQ-037 (source round trip).
GOAL-001/006.

## 3. Architecture Links

The editor owns authoring, the engine owns execution and the cooked format
([RULES](../../oxygen/RULES.md#ownership)). Related LLDs:
[documents and commands](documents-and-commands.md),
[property pipeline](property-pipeline.md),
[content browser identity](content-browser-asset-identity.md) (rows,
pickers, relocation),
[content pipeline](content-pipeline.md),
[content cooking workflows](content-cooking-workflows.md),
[cooking panel](cooking-panel.md),
[diagnostics and operation results](diagnostics-operation-results.md),
[script authoring](script-authoring.md) (asset parameters, Play section),
[material editor](material-editor.md) (document precedent).

## 4. Current Baseline

Engine, verified in source:

- **Formats.** `oxygen.input.schema.json` (`*.input.json`: optional
  `actions`, at least one `contexts` entry) and
  `oxygen.input-action.schema.json` (`*.input-action.json`: one action).
  The cooker validates against the primary schema, then the action schema.
- **Identity is the name.** An action cooks to
  `<mount>/Input/<name>.oiact` and a context to `<mount>/Input/<name>.oimap`
  (`LooseCookedLayout`); the asset key derives from that virtual path. The
  source file's folder and name play no part.
- **References are names.** Mapping `action`, trigger `chord_action` and
  combo step `action` are resolved first against the file's own `actions`,
  then against actions already in the cooked root and its
  `cooked_context_roots`. A file that uses another file's action needs that
  file's job as a `depends_on`.
- **Runtime registers by name.** `InputSystem` keys actions and contexts by
  name; hydration merges a second action of the same name and fails on a
  value-type mismatch.
- **Mapping order is evaluation order.** When a consuming action triggers,
  every later mapping in its context is cancelled and lower-priority contexts
  are not updated that frame, whatever input they are bound to
  ([EG-13](#151-engine-gaps)).
- **Triggers.** Hydration reads `hold_time` for hold, hold-and-release and
  tap (tap's release window), `interval` for pulse, `chord_action` for chord
  and action chain (both become the same runtime `ActionTriggerChain`), and
  combo steps with `completion_states` as `ActionState` flags (Ongoing 2,
  Canceled 4, Completed 8, Triggered 16) and `time_to_complete` in seconds.
  After the first step, a step's window must be positive or the combo resets.
- **A mapping without triggers never fires.**
- **Scale and bias are cooked but not applied**: hydration and
  `InputActionMapping` ignore them ([EG-1](#151-engine-gaps)). A key drives an
  axis action's X component with 1 while pressed.
- **Flags.** `auto_load`/`auto_activate` are honored by RenderScene's
  showcase loader; the player and scene instantiation ignore them and start
  the scene's authored starting contexts
  ([player](../../../projects/Oxygen.Engine/design/runtime-core/scene-instantiation-and-player.md#scene-start-state)).
- **Slots.** 145 names: keyboard keys, mouse buttons, `MouseX`/`MouseY`/
  `MouseXY`, wheel directions and axes, `AnyKey`, `None`, plus 18 aliases the
  cooker normalizes (`UpArrow` to `Up`, `LeftControl` to `LeftCtrl`).

Editor:

- The Content Browser classifies cooked `.oiact`/`.oimap` outputs as
  `InputAction` and `InputMappingContext` with filters, a shared brush and
  type glyphs (`Oxygen.Editor.ContentBrowser/src/AssetIdentity`).
- [Script authoring](script-authoring.md#4-script-assets) (ED-M08.10o)
  recognizes input files under authoring mounts, cooks one `input` job per
  file, lists their actions and contexts in pickers and opens them in the code
  editor. Script `asset` parameters and the scene's Play section reference
  them.
- `InputTranslation` (WorldEditor) maps WinUI `VirtualKey` to `RuntimeKey`
  for the viewport input bridge.
- The material editor supplies the document pattern: immutable source,
  document-owned history, atomic save, conflict and relocation participants.

## 5. Target Design

```text
Content Browser New/Open ──► InputDocumentService (one document per file)
  edit through commands ──► immutable InputSource + history + revision
  Save ──► atomic write ──► incremental cook (input job, depends_on by name)
  Rename/Move/Delete ──► relocation transaction (input files, scenes, scripts)
Script parameter / Play section pickers ──► input asset identities
Play ──► cooked scene + starting contexts in the player
```

The document is a source file. A file created by the editor holds one asset,
so its document looks like a per-asset editor; a hand-written file holding
several assets opens with an outline of them (section 9.3). Nothing is
previewed live in editor viewports: input runs in Play.

## 6. Ownership

| Owner                                   | Responsibility                                                                                                                                 |
| --------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Editor.InputEditor` (new, IQ-8) | Input documents, commands, history, save, conflicts, relocation participant, action and context editors, key selector and capture session.     |
| `Oxygen.Managed.Assets`                 | Immutable `InputSource` model, schema-validated reader and canonical writer, project input index (names to declarations and references).       |
| `Oxygen.Editor.Schemas`                 | Engine input schemas plus `oxygen.input.editor.schema.json` overlay: labels, units, steps, defaults for new items, conditional visibility.     |
| `Oxygen.Editor.ContentBrowser`          | Rows per declared asset, creation commands, details and referrers, pickers, relocation reference kinds for input.                              |
| `Oxygen.Editor.ContentPipeline`         | Input jobs, name-based `depends_on`, context roots, retirement of outputs a file no longer declares, mapping cooker diagnostics to properties. |
| `Oxygen.Editor.Runtime`                 | One key translation table (`VirtualKey` and pointer input to engine key identity), shared by the viewport bridge and key capture.              |
| WorldEditor / scripting (ED-M08.10)     | Script parameter and Play-section pickers and their persistence; unchanged by this LLD except the touch points in IQ-10.                       |
| Engine (Input, Content, Cooker, Data)   | Value types, slots, triggers, schemas, cooking, hydration and runtime behavior.                                                                |

The editor does not write cooked input, simulate triggers or define input
semantics. Fields exist only where the engine reads them.

## 7. Data Contracts

### 7.1 Authoring format

The engine formats are the authoring format, unchanged
([IQ-1](#152-open-questions)). No editor-only fields are added: the schemas
forbid additional properties.

| Asset created by the editor | File written                      | Content                                                                               |
| --------------------------- | --------------------------------- | ------------------------------------------------------------------------------------- |
| Input action `Jump`         | `<folder>/Jump.input-action.json` | `{ "name": "Jump", "type": "bool", "consumes_input": false }`                         |
| Input mapping context `Run` | `<folder>/Run.input.json`         | `{ "actions": [], "contexts": [ { "name": "Run", "priority": 0, "mappings": [] } ] }` |

The default folder is `Content/Input` ([IQ-9](#152-open-questions)).
Actions live in their own files so that any context can use them and each
has its own row, history and Rename; contexts reference them by name, which
the cook resolves through `depends_on` (section 11).

A hand-written `*.input.json` may declare several actions and contexts. Each
declaration is its own asset; the file is their shared source. The editor
edits such files in place and never splits them unasked; **Split into asset
files** (section 8.4) converts one on request.

### 7.2 Asset identity

An input asset's identity is its engine identity, the name-derived virtual
path: `asset:///<Mount>/Input/<Name>.oiact` or `.oimap`. Persisted
references (script parameter overrides, Play-section contexts) store this
URI. The source file is a row fact, like a material's descriptor path.

| Change                       | Identity  | Referrers rewritten                                                                 |
| ---------------------------- | --------- | ----------------------------------------------------------------------------------- |
| Move a file within its mount | Unchanged | None; only the file moves.                                                          |
| Move a file to another mount | Changes   | Scenes and scripts (URI); input files need no change (names resolve across mounts). |
| Rename an action             | Changes   | Input files (mapping, chord and combo names), scenes and scripts.                   |
| Rename a context             | Changes   | Scenes and scripts.                                                                 |

Names are unique across the project's mounts for actions and, separately,
for contexts, because the runtime registers both by name. An action declared
identically in two files is one asset with two sources (warning); with
different types it is an error, as in the cooker. Two contexts with the same
name are an error (the cooker would silently let one overwrite the other,
[EG-3](#151-engine-gaps)).

A name is 1–63 characters matching `[A-Za-z_][A-Za-z0-9_]*`, which keeps
virtual paths, file names and script references simple
([IQ-4](#152-open-questions)). Hand-written names outside that pattern open
with a warning and are kept until renamed. For an editor-created file the
file stem equals the name; Rename keeps them equal. A file whose stem differs
(hand-written, or holding several assets) keeps its file name.

### 7.3 Model

`InputSource` is an immutable typed projection of one file, read and written
only through the engine schemas. Collection items carry session-only element
IDs so that edit sessions, selection and history survive inserts and
reorders; IDs are never written.

```text
InputSource(Kind: ActionFile | InputFile, Path, Actions[], Contexts[])
InputActionDecl(Id, Name, ValueType: Bool|Axis1D|Axis2D, ConsumesInput)
InputContextDecl(Id, Name, Priority:int32, AutoLoad, AutoActivate, Mappings[])
InputMapping(Id, ActionName, Slot, Scale:(x,y), Bias:(x,y), Triggers[],
             WrittenAsShorthand)
InputTrigger(Id, Type, Behavior, ActuationThreshold,
             HoldTime?, Interval?, LinkedActionName?, ComboSteps[])
ComboStep(Id, ActionName, CompletionStates:flags, TimeToComplete)
```

Reading normalizes nothing that changes the file: a slot alias, the
`trigger` shorthand, `aux` versus `combo_actions` and omitted defaults are
remembered so that an unedited file saves identically apart from key order
and whitespace. Editing an item writes its canonical form: canonical slot
name, `triggers` array (the shorthand is kept while the mapping still has
exactly that one default trigger), `combo_actions`.

### 7.4 Fields

Seconds display with an `s` suffix and three decimals. Defaults are for items
the editor creates; reading keeps the engine defaults for omitted fields.

| Field                              | File path                           | New default                        | Validation                                                      |
| ---------------------------------- | ----------------------------------- | ---------------------------------- | --------------------------------------------------------------- |
| Action name                        | `name`                              | entered at creation                | section 7.2                                                     |
| Value type                         | `type`                              | Digital (`bool`)                   | enum                                                            |
| Consume input                      | `consumes_input`                    | off                                | bool                                                            |
| Context name                       | `contexts[].name`                   | entered at creation                | section 7.2                                                     |
| Priority                           | `contexts[].priority`               | 0                                  | int32; higher runs first                                        |
| Load when mounted                  | `auto_load`                         | off                                | bool                                                            |
| Activate when loaded               | `auto_activate`                     | off                                | requires Load when mounted (schema)                             |
| Input                              | `mappings[].slot`                   | chosen when added                  | a known slot; `None` shows as Unbound                           |
| Scale, bias                        | `mappings[].scale`, `bias`          | (1,1), (0,0)                       | finite; see [IQ-2](#152-open-questions)                         |
| Trigger type                       | `triggers[].type`                   | Pressed for Digital, Down for axes | enum; Chord covers `action_chain` ([IQ-5](#152-open-questions)) |
| Behavior                           | `triggers[].behavior`               | Implicit                           | enum                                                            |
| Actuation threshold                | `triggers[].actuation_threshold`    | 0.5                                | finite, ≥ 0                                                     |
| Hold time (Hold, Hold and release) | `hold_time`                         | 0.5 s                              | > 0 (schema requires it)                                        |
| Tap time (Tap)                     | `hold_time`                         | 0.2 s                              | > 0                                                             |
| Interval (Pulse)                   | `interval`                          | 0.25 s                             | > 0 (schema requires it)                                        |
| Chord action (Chord)               | `chord_action`                      | none; must be picked               | an existing action; not the mapping's own                       |
| Combo steps (Combo)                | `combo_actions[]`                   | one empty step                     | at least one step                                               |
| Step action                        | `combo_actions[].action`            | none; must be picked               | an existing action                                              |
| Step completes on                  | `combo_actions[].completion_states` | Triggered                          | at least one of Triggered, Completed, Ongoing, Canceled         |
| Step window                        | `combo_actions[].time_to_complete`  | 0.5 s                              | ≥ 0; > 0 for every step after the first                         |

Rules the schema does not express are editor checks with `OXE.INPUT.*`
codes (section 12) and are proposed as schema additions
([EG-2](#151-engine-gaps)).

## 8. Commands, Services, Or Adapters

### 8.1 Documents and history

`IInputDocumentService` follows the material document contract
([material editor 8.1, 17, 18](material-editor.md#81-material-commands)):
open by file, edit under the authoring lock, one history entry per committed
gesture, revision before asynchronous work, atomic save with saved-revision
acknowledgement, close drains persistence. It implements
`IDocumentConflictParticipant` (an external edit offers Reload or Save copy)
and the relocation participant (an open document re-points its path and the
names it references without closing).

Opening an action or context row opens its file's document and selects that
asset. A file already open is activated, not opened twice.

### 8.2 Edits

Field edits go through the property pipeline: `PropertyId` is the input
schema kind plus a pointer built from element IDs, resolved to the JSON
pointer at commit. Structural edits are document commands with before and
after `InputSource` snapshots:

| Command                                                   | One history entry                                        |
| --------------------------------------------------------- | -------------------------------------------------------- |
| Add action to context (with first mapping), remove action | Adds or removes all of that action's mappings.           |
| Add, duplicate, remove, reorder mapping                   | Per command; multi-selection is one entry.               |
| Set input (pick or capture)                               | Per binding.                                             |
| Add, remove, reorder trigger; change trigger type         | Type change keeps behavior and threshold, drops others.  |
| Add, remove, reorder combo step                           | Per command.                                             |
| Toggle Load when mounted off                              | Also clears Activate when loaded.                        |
| Paste mappings                                            | Appends after the selection; missing actions stay named. |
| Group by action                                           | Makes each action's mappings contiguous (changes order). |

Rename, move and delete are Content Browser relocations, outside document
history (section 8.4).

### 8.3 Creation

**New ▸ Input ▸ Input Action** and **Input Mapping Context** ask for a name
in place (validated as typed), write the file through the atomic store, select
the row and open the editor. The same commands appear in the action picker
(New action, created in the context's folder and added to the context in one
step), in script `asset` parameter pickers and in the Play section's context
picker ([IQ-10](#152-open-questions)). Creating a file does not cook it; the
first Save or a demand from a picker does.

### 8.4 Rename, move, delete, split

These run as the
[relocation transaction](content-browser-asset-identity.md#95-asset-relocation)
(journal, atomic writes, rollback, Undo from the result). The reference index
gains three input kinds ([IQ-10](#152-open-questions)): action names in input
files (mapping `action`, `chord_action`, combo step `action`), context URIs in
scene Play sections, and input asset URIs in script parameter overrides.

- **Rename** an action or context: the name changes in its file and in every
  referring input file, scene and script; an editor-created file is renamed
  with it. Collisions and invalid names are rejected before anything is
  written.
- **Move** acts on the source file. For a file holding several assets the
  command says so ("Move file with 4 input assets"). Section 7.2 decides
  which referrers change.
- **Delete** lists referrers and confirms when any exist. An asset alone in
  its file deletes the file; an asset in a shared file is removed from it.
  Removing the last context of an `*.input.json` is rejected
  (`OXE.INPUT.LastContextInFile`) with Delete file and Split as the offered
  actions, since the schema requires one context. Remaining references show
  as missing and fail their cook.
- **Split into asset files** (on a multi-asset file): writes one
  `*.input-action.json` per action and one `*.input.json` per context in the
  same folder, then deletes the original. Identities do not change, so no
  referrer is rewritten. One relocation-style transaction with Undo.

### 8.5 Key capture

`KeyCaptureSession` is a view-model object that consumes abstract events and
yields a slot or nothing; the view feeds it from routed events of its capture
surface. Tests drive it with synthesized events, without real input, focus or
a window.

```text
CaptureEvent = Key(VirtualKey, ScanCode, IsExtended)
             | PointerButton(Left|Right|Middle|X1|X2)
             | Wheel(dx, dy) | Cancel
```

| Event                                 | Result                                                                                       |
| ------------------------------------- | -------------------------------------------------------------------------------------------- |
| Key down                              | Slot from the shared translation; left and right modifiers from scan code and extended flag. |
| Escape                                | Cancels capture. Escape itself is picked from the list.                                      |
| Pointer button on the capture surface | Mouse button slot; the click that started capture is ignored.                                |
| Wheel notch                           | `MouseWheelUp`/`Down`/`Left`/`Right` by sign and axis.                                       |
| Focus lost, Cancel                    | Cancels with no change.                                                                      |
| Key with no engine slot               | Stays in capture and shows "This key has no Oxygen input" (`OXE.INPUT.CaptureUnsupported`).  |

Mouse movement and wheel axes (`MouseX`, `MouseXY`, `MouseWheelY`...) are
picked from the list: motion is too ambiguous to capture. Capture reads only
events routed to the editor window; it installs no hooks.

### 8.6 Validation service

`InputValidation` runs on load, after every commit and before cook, over the
document and the project input index (names, duplicates, unresolved
references). Results attach to fields and rows; they do not block editing.
Errors block that file's cook with the same messages.

## 9. UI Surfaces

### 9.1 Content Browser

- **Rows.** One row per declared action and context, in the folder of its
  source file. When a file holds more than one asset, rows show "in
  Player.input.json" under the name. A file that fails schema validation shows
  one Broken row named after the file, with the schema messages and **Open in
  code editor**.
- **Tiles.** Generated, no renderer: the action tile shows its value-type
  glyph (button, slider, two-axis pad) and the consume-input mark; the context
  tile shows up to four key caps of its first bindings and the mapping count.
  Both kinds keep the input brush; the glyphs distinguish them.
- **New ▸ Input** menu with the two creation commands; the context menu mirrors
  it. **Open in code editor** stays available on every input row.
- **Details.** Action: value type, consume input, source file, and "Used by":
  contexts with their bindings (`Gameplay — Space, Pressed`), scripts and
  scenes, each opening its user. Context: priority, mapping and action counts,
  flags, source file, and scenes and scripts that start or switch it.
- **Filters.** The existing Input actions and Input mapping contexts filters
  include source rows.

### 9.2 Action editor

```text
+-----------------------------------------------------------------------+
| [glyph] Jump      Input action · Content/Input/Jump.input-action.json |
|                                                    [Needs cooking]    |
+-----------------------------------------------------------------------+
| Value type     [ Digital | Axis 1D | Axis 2D ]                         |
| Consume input  [x]  When it triggers, later mappings in its context   |
|                     and lower-priority contexts are skipped that       |
|                     frame, whatever their input.                       |
+-----------------------------------------------------------------------+
| Used by                                                                |
|   Gameplay   Space · Pressed                         [Open]            |
|   Vehicle    Space · Pressed, Hold 0.400 s           [Open]            |
|   Scripts    player.lua · jump_action                [Open]            |
+-----------------------------------------------------------------------+
```

Changing the value type of a used action shows, inline, what the engine does
with each binding (a key on an axis action drives X with 1 while pressed); it
is not blocked.

### 9.3 Context editor

```text
+----------------------------------------------------------------------------------+
| [glyph] Gameplay   Input mapping context · Content/Input/Gameplay.input.json      |
| Priority [ 100 ]  ▸ Standalone loading                          [Cooked]          |
+----------------------------------------+-----------------------------------------+
| Mappings · evaluation order  [+ Action] | Jump · Space                            |
| ▾ Jump              Digital   [+] [⋯]   | Input     [ Space          ] [⌨ Press]  |
|     [Space]   Pressed                    | Triggers                      [+ Add ▾] |
|     [F]       Pressed · Hold             |  ▾ Pressed  [Implicit|Explicit|Blocker] |
| ▾ MoveForward       Axis 1D   [+] [⋯]   |      Actuation threshold  0.50          |
|     [W]       Down                       |  ▾ Hold     [Implicit|Explicit|Blocker] |
|     [Up]      Down                       |      Actuation threshold  0.50          |
| ▾ Look              Axis 2D   [+] [⋯]   |      Hold time            0.400 s       |
|     [Mouse XY] ⚠ No triggers: never fires|                                         |
+----------------------------------------+-----------------------------------------+
```

- **Header.** Priority (higher runs first; the Play section starts with this
  value unless it overrides it). **Standalone loading**, collapsed, holds Load
  when mounted and Activate when loaded with one line: "Used by runtimes that
  load every mounted context. Play starts the scene's starting input
  contexts." ([IQ-6](#152-open-questions)).
- **Mappings list.** Evaluation order, top to bottom, because order decides
  which mapping consumes input first. Consecutive mappings of one action form a
  group with the action's name and value type; the group header adds a binding
  (`+`), opens the action, removes the action's mappings or moves the group.
  An action whose mappings are not contiguous appears in several groups, and
  **Group by action** in the list menu joins them after saying that it changes
  evaluation order. New bindings join their action's last group. Drag reorders
  rows and groups.
- **Rows.** A key cap (click opens the key selector), then trigger chips, then
  a badge for errors and warnings. A missing action shows its name in error
  style with **Pick action** and **Remove mappings**.
- **+ Action** opens the action picker (search, value type, source file; New
  action). Choosing adds the action with one binding that opens the key
  selector.
- **Details pane.** The selected binding's input, then Scale X/Y and Bias X/Y
  (only once the runtime applies them, IQ-2), then its triggers. Triggers are an
  ordered list of cards: a type dropdown, a three-way behavior selector,
  remove and move. Each type shows only the fields the engine reads (table 7.4).
  Combo shows its steps as a table: action picker, "completes on" flag chips,
  window in seconds, move and remove. Chord shows an action picker. Tooltips
  state the engine's meaning: Implicit triggers must all trigger; with any
  Explicit trigger, at least one Explicit must trigger; a triggered Blocker
  stops the mapping.
- **Multi-selection.** Several rows: Delete, Duplicate, Copy, Move up/down, and
  scale and bias with mixed values. Inputs and triggers edit one row at a time;
  the pane says "Select one binding to edit its input and triggers".
- **Outline.** A file with local actions or more than one context shows a left
  outline listing them; selecting one shows its editor. Editor-created files
  hide it.
- **Clipboard.** Copy and paste of bindings between contexts as their schema
  JSON; pasting into another document keeps action names.

### 9.4 Key selector

```text
+------------------------------------------+
| [ ⌨ Press a key or mouse button… ]       |
| [ Search keys…                       ]   |
| Keyboard  Letters · Digits · Function    |
|           Navigation · Modifiers · Keypad |
|           Symbols · Editing               |
| Mouse     Buttons · Movement · Wheel      |
| Special   Any key · Unbound               |
+------------------------------------------+
```

A flyout from the key cap. Labels are readable (`Left Ctrl`, `Keypad 7`,
`Mouse X`, `Wheel up`) with the slot name as secondary text; search matches
both. Each entry shows what the engine sends: a button value, a 1D delta or a
2D delta. Pressing **Press a key** starts the capture session (section 8.5);
the button reads "Press a key… Esc to cancel" while waiting.

### 9.5 State and diagnostics

The document header shows the shared cook state chip (Needs cooking, Out of
date, Cooked, Failed) as in the material editor. Field errors show inline;
the list badge and the row's tooltip carry the first message; the cooking
panel lists cook failures with Go To Property.

## 10. Persistence And Round Trip

- The writer emits schema-valid JSON, 2-space indentation, keys in schema
  order, `$schema` kept when present.
- An unedited file reopens and saves to the same values; an edited item is
  written canonically (section 7.3). The three example input files under
  `Examples/Content` round-trip with identical cooked descriptors.
- Element IDs, selection, outline state and capture state are not persisted.
- Missing actions in mappings stay as authored names.
- Saving never cooks synchronously and never saves other documents.

## 11. Live Sync / Cook / Runtime Behavior

- **Trigger.** Successful Save queues an incremental cook of that file's job
  ([cooking workflows §4](content-cooking-workflows.md#4-cooking-triggers));
  picker demand cooks the saved closure. No live projection: editor viewports
  do not run game input.
- **Jobs.** One `input` job per file. The project input index adds
  `depends_on` on the jobs of the files that declare every action the file
  uses by name, and passes other mounts' roots as `cooked_context_roots` in
  content priority order. A changed action file makes its dependents stale.
- **Outputs.** An input job has several outputs. Provenance records the
  emitted keys per file; after a rename or removal, outputs the file no longer
  emits are retired with the existing retirement path, so old names leave the
  index.
- **Failures.** The cooker's `input.*` diagnostics keep their codes and map to
  properties through their object path; both forms the cooker emits
  (`$.contexts[0].mappings[2]` and `contexts.Gameplay.mappings[2]`) resolve
  ([EG-11](#151-engine-gaps)). A dirty input document in a cook closure follows
  the Save listed and Cook flow.
- **Play.** The Play section's contexts and script references resolve to the
  cooked assets; the player hydrates them by key. Nothing in Play reads the
  Load and Activate flags.

## 12. Operation Results And Diagnostics

Operations: `Input.Create`, `Input.Open`, `Input.Edit`, `Input.Save`,
`Input.Cook`, `Input.Rename`, `Input.Move`, `Input.Delete`, `Input.Split`.
Domain `InputAuthoring`; persistence uses `Document`, cooking `AssetCook`,
relocation its existing domain.

| Code                               | Severity | When                                                                       |
| ---------------------------------- | -------- | -------------------------------------------------------------------------- |
| `OXE.INPUT.SOURCE_INVALID`         | Error    | The file fails its schema; row is Broken; editing is disabled.             |
| `OXE.INPUT.NAME_INVALID`           | Error    | Empty, longer than 63 characters or outside the name pattern (new names).  |
| `OXE.INPUT.NAME_PATTERN`           | Warning  | A hand-written name outside the pattern.                                   |
| `OXE.INPUT.NAME_NOT_UNIQUE`        | Error    | Two contexts, or two differently typed actions, share a name.              |
| `OXE.INPUT.DUPLICATE_DECLARATION`  | Warning  | One action declared identically in two files.                              |
| `OXE.INPUT.ACTION_MISSING`         | Error    | A mapping, chord or combo step names no declared action.                   |
| `OXE.INPUT.SLOT_UNKNOWN`           | Error    | A slot name the engine does not know.                                      |
| `OXE.INPUT.MAPPING_NO_TRIGGERS`    | Warning  | A mapping with no triggers never fires.                                    |
| `OXE.INPUT.TRIGGER_FIELD_REQUIRED` | Error    | Hold, tap or pulse time not positive; chord action or combo steps missing. |
| `OXE.INPUT.COMBO_STEP_UNREACHABLE` | Error    | A step with no completion state, or a zero window after the first step.    |
| `OXE.INPUT.VALUE_OUT_OF_RANGE`     | Error    | Non-finite or out-of-range number; the edit is rejected.                   |
| `OXE.INPUT.SCALE_BIAS_IGNORED`     | Info     | Non-default scale or bias, which the runtime does not apply (until EG-1).  |
| `OXE.INPUT.LAST_CONTEXT_IN_FILE`   | Error    | Delete would leave an `*.input.json` without contexts.                     |
| `OXE.INPUT.CAPTURE_UNSUPPORTED`    | Info     | A captured key has no engine slot.                                         |
| `OXE.INPUT.CREATE_FAILED`          | Error    | Creating a file failed; names the path and cause.                          |

Diagnostics carry the document, file path, asset URI and property path.
Engine `input.*` codes stay as reported.

## 13. Dependency Rules

- InputEditor depends on Documents, Schemas, Managed.Assets, ContentBrowser
  pickers and ContentPipeline services; nothing depends on InputEditor except
  the app's composition root.
- Managed.Assets owns the reader, writer and index; ContentPipeline and
  ContentBrowser consume them rather than parsing input files themselves.
- Key translation exists once (Runtime); WorldEditor's viewport bridge and key
  capture both use it.
- No editor code reads `.oiact`/`.oimap` bytes; cooked facts come from the
  loose index.

## 14. Validation Gates

| ID   | Check                                                                                                                                                        |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| IA-1 | Creating an action and a context writes schema-valid files whose cook produces the expected keys; reader and writer tests cover every field and default.     |
| IA-2 | The example input files open, save unchanged in value and cook to identical descriptors; aliases, shorthand and `aux` survive until edited.                  |
| IA-3 | Key capture session tests with synthesized events: letters, digits, both-side modifiers, keypad and extended keys, mouse buttons, wheel, Escape, focus loss. |
| IA-4 | Every trigger type's fields edit, validate and round-trip; type change, reorder, combo steps and flags undo and redo as single entries.                      |
| IA-5 | Rename, move, delete and split retarget input files, Play sections and script overrides as in section 7.2; relocation Undo restores all files.               |
| IA-6 | Gesture, undo/redo, dirty, save-then-edit and external-change conflict behavior match the material document contract.                                        |
| IA-7 | Save cooks the file and its dependents; a cross-file action resolves; a renamed action's old output leaves the index; cook errors navigate to the field.     |
| IA-8 | A scene whose Play section starts an editor-authored context runs in the player, and a script's `on_action` handler fires for a bound key.                   |
| IA-9 | Owner review of SC1–SC10 in the running editor.                                                                                                              |

## 15. Open Issues

### 15.1 Engine gaps

Capabilities the engine does not provide, which this design therefore does
not offer, and engine facts that limit authoring.

| ID    | Gap                                                                                                                                                                         | Editor handling                                              | Matters for V0.1                                                                                             |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------ |
| EG-1  | Scale and bias are cooked but not applied by hydration or mappings.                                                                                                         | Per IQ-2; non-default values kept and flagged.               | Yes: without them keys cannot build a 2D move axis or a negative 1D axis, and mouse look has no sensitivity. |
| EG-2  | The schema omits rules the runtime depends on: tap time, chord link for `action_chain`, combo step states and windows, threshold sign, non-empty triggers.                  | Editor checks with `OXE.INPUT.*` codes.                      | Yes, schema-only: hand-written files and cooks should fail the same way the editor does.                     |
| EG-3  | The cooker does not detect two contexts of the same name in different files; the later one overwrites.                                                                      | Editor error.                                                | Low: the editor prevents it.                                                                                 |
| EG-4  | Runtime trigger options absent from the cooked format: hold one-shot; pulse trigger-on-start, limit, jitter, phase, ramp; chain max delay and require-held; combo breakers. | Not offered.                                                 | No; combo breakers and hold one-shot are the first candidates after V0.1.                                    |
| EG-5  | No input modifiers beyond scale and bias (dead zone, smoothing, response curves).                                                                                           | Not offered.                                                 | No; negate and swizzle follow from EG-1.                                                                     |
| EG-6  | No triggers on the action itself shared by all its mappings.                                                                                                                | Not offered; per-binding triggers only.                      | No.                                                                                                          |
| EG-7  | No 3D axis value type.                                                                                                                                                      | Not offered.                                                 | No.                                                                                                          |
| EG-8  | No gamepad slots.                                                                                                                                                           | Not offered; the key selector has no gamepad section.        | No (out of scope).                                                                                           |
| EG-9  | No player-remappable bindings or runtime rebinding.                                                                                                                         | Not offered.                                                 | No (out of scope).                                                                                           |
| EG-10 | No description field for authors' notes (the schemas reject extra properties).                                                                                              | None.                                                        | Low.                                                                                                         |
| EG-11 | Cooker object paths use two forms (`$.contexts[i]...` and `contexts.<name>...`).                                                                                            | Both resolve to properties.                                  | Low.                                                                                                         |
| EG-12 | No API to evaluate a context against synthetic input outside a running game.                                                                                                | No in-editor preview (IQ-7).                                 | No.                                                                                                          |
| EG-13 | Consumption is not scoped to the consumed input: a consuming trigger skips every later mapping and lower-priority context for the frame.                                    | Stated in the Consume input help and the mapping list order. | Worth confirming: authors usually expect only the same key to be consumed.                                   |

### 15.2 Open questions

In priority order. Each needs the owner's decision at ED-M09.1.

| ID    | Question                                                             | Recommendation                                                                                                                                                                                                                                                                                                                                                                                                                    |
| ----- | -------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IQ-1  | Authoring format and file granularity.                               | Engine formats unchanged; the editor creates one asset per file; hand-written multi-asset files are edited in place, with explicit Split (sections 7.1, 8.4).                                                                                                                                                                                                                                                                     |
| IQ-2  | Scale and bias have no runtime effect (EG-1).                        | Schedule the engine fix (apply scale and bias in mappings) in ED-M09.2 and show Scale X/Y and Bias X/Y per binding in the details pane. If declined: hide them, keep hand-written values and show `SCALE_BIAS_IGNORED`.                                                                                                                                                                                                           |
| IQ-3  | Input asset identity and name scope.                                 | Name-derived virtual path (section 7.2); names unique project-wide per kind; moves within a mount rewrite nothing.                                                                                                                                                                                                                                                                                                                |
| IQ-4  | Name pattern.                                                        | `[A-Za-z_][A-Za-z0-9_]*`, 1–63 characters, for new names and renames; hand-written exceptions kept with a warning.                                                                                                                                                                                                                                                                                                                |
| IQ-5  | Chord and action chain behave identically at runtime.                | One "Chord" type in the picker, written as `chord`; hand-written `action_chain` shows as Chord and is kept until its type is changed.                                                                                                                                                                                                                                                                                             |
| IQ-6  | Show Load when mounted and Activate when loaded, which Play ignores? | Show, collapsed under Standalone loading, with the one-line explanation. Alternative: hide and preserve.                                                                                                                                                                                                                                                                                                                          |
| IQ-7  | In-editor live preview of a context.                                 | Defer past V0.1; Play is the test path (EG-12).                                                                                                                                                                                                                                                                                                                                                                                   |
| IQ-8  | Module placement.                                                    | New `Oxygen.Editor.InputEditor` project beside MaterialEditor; ARCHITECTURE and project layout list it.                                                                                                                                                                                                                                                                                                                           |
| IQ-9  | Default folder.                                                      | `Content/Input`, created by the project template and used by the creation target policy ([project layout](project-layout-and-templates.md)).                                                                                                                                                                                                                                                                                      |
| IQ-10 | Touch points in other LLDs (not edited here).                        | At ED-M09.1 close, amend: [script authoring](script-authoring.md) §4 (Edit opens the input editor; Open in code editor remains), §6 and §8 (pickers offer New action and New context; Play priority default reads the source); [content browser §9.5](content-browser-asset-identity.md#95-asset-relocation) (input reference kinds); [content pipeline](content-pipeline.md) (name-based `depends_on`, multi-output provenance). |
| IQ-11 | Mapping order.                                                       | Show evaluation order with contiguous action groups and an explicit Group by action (section 9.3), rather than a grouped view that hides order.                                                                                                                                                                                                                                                                                   |
| IQ-12 | Schema augmentation (EG-2).                                          | Engine schema-only change in ED-M09.2 so the cooker rejects what the editor rejects.                                                                                                                                                                                                                                                                                                                                              |

### 15.3 Rejected alternatives

- **Editor-owned input format** (per-asset files with GUID references):
  a parallel schema the cooker cannot read and a second identity scheme beside
  the engine's names.
- **One project-wide input file edited as one document**: one undo stack and
  one conflict surface for unrelated contexts, and no per-asset rows.
- **Hand-written files read-only in the editor**: two workflows for the same
  assets; scripting already makes hand-written files first-class.
- **Splitting multi-asset files on open**: rewrites user files unasked.
- **GUID or path references inside input files**: the engine resolves names.
- **An editor simulation of triggers for preview**: a second implementation of
  engine semantics that would drift.
- **Exposing scale and bias while the runtime ignores them**: saved and live
  semantics would differ (pending IQ-2).
- **A grouped mapping view that hides order**: order decides consumption.
- **Capture through global keyboard hooks**: needs real input and focus, cannot
  be tested headlessly and reaches outside the editor.
