# Input Authoring LLD

Status: `design approved (ED-M10.1, 2026-10-11)`; identity rewritten to the
input source contract (2026-10-19); delivered by
[ED-M10](../plan/ED-M10-input-authoring.md).

Last Updated: 2026-10-20 01:32

Summary: users author input actions and input mapping contexts in input sources,
the engine's `*.input.json` files, from the Content Explorer and two focused
editors. An input source is a container, like a folder: each action and context
it declares is an asset named under the source
(`/Content/Input/Player.input/Jump.oiact`), and the Content Explorer shows the
source; showing its assets inside it is
[#80](https://github.com/abdes/DroidNet/issues/80). A context lists its mappings
in evaluation order, grouped by action; each mapping binds one keyboard or mouse
input, captured by pressing it or picked from a list, and carries an ordered
list of triggers edited as typed cards. A mapping names an action of its own
source by name and an action of another source by its path. Renaming an action
or a context, and renaming or moving a source, retarget every source, scene and
script that refers to them. Hand-written sources open in the same editors. Save
cooks; Play runs the result. The engine owns the input model, the format and the
cooker ([input cooking architecture][input-cooking]); the identity of input
assets is the [asset identity contract][aid] (1.2 to 1.4); this LLD owns the
authoring workflow. Read [scenarios](#1-purpose),
[format and identity](#7-data-contracts), [editors](#9-ui-surfaces),
[engine gaps](#151-engine-gaps) and [open questions](#152-open-questions).

## 1. Purpose

Users author the input actions and mapping contexts their scenes and scripts
use, without writing JSON. Scenarios:

| ID   | Scenario                                                                                                                                                                                     |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SC1  | Create an input source, then an input action in it; choose the action's value type and whether it consumes input.                                                                            |
| SC2  | Create an input mapping context in an input source; set its priority.                                                                                                                        |
| SC3  | Add an action to a context and bind keys and mouse input by pressing them or picking them from a list.                                                                                       |
| SC4  | Add, reorder, configure and remove triggers on a mapping, including hold, tap, pulse, action chain and combo.                                                                                |
| SC5  | Rename an action or context, rename or move its source, move it to another source, or delete it; every source, scene Play section and script parameter that refers to it follows or reports. |
| SC6  | Open an existing hand-written input source, including one that declares several actions and contexts, and edit it in place.                                                                  |
| SC7  | Undo and redo every edit; the document shows unsaved changes until saved.                                                                                                                    |
| SC8  | Save; the file cooks; errors appear on the field that caused them.                                                                                                                           |
| SC9  | Pick the action in a script parameter and the context in the scene's Play section; press Play; the script receives the action.                                                               |
| SC11 | Map an action that another source declares, such as a shared `Common.input.json`, from a context in this source.                                                                             |
| SC10 | See an asset's users (contexts, scenes, scripts) before changing or deleting it.                                                                                                             |

## 2. PRD Traceability

REQ-045 (this design); REQ-043 and REQ-044 consume its assets (script
parameters, Play section). REQ-006/007 (undo, dirty, round trip), REQ-021/022
(shared asset state, visible failures), REQ-037 (source round trip).
GOAL-001/006.

## 3. Architecture Links

The editor owns authoring, the engine owns execution and the cooked format
([RULES](../../oxygen/RULES.md#ownership)). Module ownership of the input family
is in [asset family modules](asset-family-modules.md); identities, references
and collisions in [asset identity](asset-identity.md). Related LLDs:
[documents and commands](documents-and-commands.md),
[property pipeline](property-pipeline.md),
[content explorer identity](content-explorer-asset-identity.md) (rows, pickers,
relocation), [content pipeline](content-pipeline.md),
[content cooking workflows](content-cooking-workflows.md),
[cooking panel](cooking-panel.md),
[diagnostics and operation results](diagnostics-operation-results.md),
[script authoring](script-authoring.md) (asset parameters, Play section),
[material editor](material-editor.md) (document precedent).

## 4. Current Baseline

Engine, verified in source:

- **Format.** One schema, `oxygen.input.schema.json` (`*.input.json`): optional
  `actions` and optional `contexts`, both defaulting to empty. A source with
  actions only is an input source like any other.
- **Products under the source.** An input job names its source's identity, the
  path without the authoring `.json` (`/Content/Input/Player.input`); each
  action cooks to `<source identity>/<name>.oiact` and each context to
  `<source identity>/<name>.oimap`, and the asset key derives from that virtual
  path. Two sources in one folder that declare `Jump` cook two assets.
- **References.** Mapping `action`, trigger `chord_action` and combo step
  `action` name an action of the same source by name, or any action by virtual
  path (`/Content/Input/Common.input/Jump.oiact`), resolved through the mounted
  cooked roots like a scene reference (`input.context.action_unresolved`,
  `input.context.action_type_mismatch`).
- **Runtime identity is the key.** The runtime finds actions and contexts by
  asset key; a name is a label for display and logs, and script input events
  name the action asset.
- **Mapping order is evaluation order.** When a consuming action triggers, later
  mappings in its context and mappings in lower-priority contexts are skipped
  for that event only if their slot shares a physical source with the consumer;
  an active one is cancelled, an idle one drops its input silently ([IF-9][mf],
  delivered).
- **Triggers.** Hydration reads `hold_time` for hold, hold-and-release and tap
  (tap's release window), `interval` for pulse, `chord_action` for chord and
  action chain (both become the same runtime `ActionTriggerChain`, [IF-10][mf]),
  and combo steps with `completion_states` as `ActionState` flags (Ongoing 2,
  Canceled 4, Completed 8, Triggered 16) and `time_to_complete` in seconds.
  After the first step, a step's window must be positive or the combo resets.
- **A mapping without triggers never fires.**
- **Scale and bias apply per component** (`value * scale + bias`; no bias on
  mouse and wheel deltas). A key feeds both components, so W with scale (0, 1)
  gives (0, 1) and an unscaled key on an Axis 2D action gives (1, 1)
  ([IF-1][mf], delivered).
- **Several mappings of one action combine** per the action's `accumulation`:
  `highest_absolute` (default; W and Up Arrow do not stack) or `cumulative`
  (sum; W and S cancel out).
- **Starting contexts.** A scene's starting input contexts, authored in its Play
  section, are the only way contexts start ([player][player-start]). Contexts
  have no `auto_load` or `auto_activate` flags in the schema, cooker or cooked
  format, so this design has no such fields.
- **Slots.** 145 names: keyboard keys, mouse buttons, `MouseX`/`MouseY`/
  `MouseXY`, wheel directions and axes, `AnyKey`, `None`, plus 18 aliases the
  cooker normalizes (`UpArrow` to `Up`, `LeftControl` to `LeftCtrl`).

Editor:

- The Content Explorer classifies cooked `.oiact`/`.oimap` outputs as
  `InputAction` and `InputMappingContext` with filters, a shared brush and type
  glyphs (`Oxygen.Editor.ContentExplorer/src/Panes/Assets`), and an input source
  as an `InputFile` row.
- [Script authoring](script-authoring.md#4-script-assets) (ED-M08.10o) creates
  input sources from a template, opens them in the code editor and checks them
  on save. Script `asset` parameters and the scene's Play section reference
  their actions and contexts.
- `InputSource` (Managed.Assets) reads a source against the embedded engine
  schema with the position of every declaration and reference, reports duplicate
  and undeclared names at their position, and renames an action or context
  together with its local references.
- [Content pipeline §24](content-pipeline.md#24-input-sources) cooks one `input`
  job per source named by its identity, orders sources by their path references,
  and finds a product's declaration in the source its identity names
  (`InputSourceFiles`). Relocation moves a source's products with it and renames
  a product inside its source (`InputProductRename`).
- `InputTranslation` (WorldEditor) maps WinUI `VirtualKey` to `RuntimeKey` for
  the viewport input bridge.
- The material editor supplies the document pattern: immutable source,
  document-owned history, atomic save, conflict and relocation participants.

## 5. Target Design

```text
Content Explorer New/Open ──► InputDocumentService (one document per source)
  edit through commands ──► immutable InputSource + history + revision
  Save ──► atomic write ──► incremental cook (input job; depends_on
            from path references)
  Rename/Move/Delete ──► relocation transaction (sources, scenes, scripts)
Script parameter / Play section pickers ──► input asset identities
Play ──► cooked scene + starting contexts in the player
```

The document is an input source. It opens on the action or context the user
picked, with an outline of everything the source declares (section 9.3). Nothing
is previewed live in editor viewports: input runs in Play.

## 6. Ownership

| Owner                                   | Responsibility                                                                                                                                                                                                                       |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Oxygen.Editor.InputEditor` (new, IQ-8) | Input documents, commands, history, save, conflicts, relocation participant, action and context editors, key selector and capture session.                                                                                           |
| `Oxygen.Editor.Input`                   | The input family module: `InputContainer` with its `InputAction` and `InputMappingContext`, input rules and starting context priorities, input facts (`IInputSources`), templates ([asset family modules](asset-family-modules.md)). |
| `Oxygen.Managed.Assets`                 | Immutable `InputSource` model, schema-validated reader with positions, and canonical writer, including product renames with their local references.                                                                                  |
| `Oxygen.Editor.Schemas`                 | Engine input schemas plus `oxygen.input.editor.schema.json` overlay: labels, units, steps, defaults for new items, conditional visibility.                                                                                           |
| `Oxygen.Editor.ContentExplorer`         | The source as one row (its actions and contexts inside it: [#80](https://github.com/abdes/DroidNet/issues/80)), creation commands, details and referrers, pickers.                                                                   |
| `Oxygen.Editor.ContentPipeline`         | Input jobs named by their source, `depends_on` and context roots from path references, product renames and moves in relocation, retirement of outputs a source no longer declares, cooker diagnostics mapped to properties.          |
| `Oxygen.Editor.Runtime`                 | One key translation table (`VirtualKey` and pointer input to engine key identity), shared by the viewport bridge and key capture.                                                                                                    |
| WorldEditor / scripting (ED-M08.10)     | Script parameter and Play-section pickers and their persistence; unchanged by this LLD except the touch points in IQ-10.                                                                                                             |
| Engine (Input, Content, Cooker, Data)   | Value types, slots, triggers, schemas, cooking, hydration and runtime behavior.                                                                                                                                                      |

The editor does not write cooked input, simulate triggers or define input
semantics. Fields exist only where the engine reads them.

## 7. Data Contracts

### 7.1 Authoring format

The engine format is the authoring format, unchanged
([IQ-1](#152-open-questions)). No editor-only fields are added: the schema
forbids additional properties.

| Created by the editor          | Written                                                                                   |
| ------------------------------ | ----------------------------------------------------------------------------------------- |
| Input source `Player`          | `<folder>/Player.input.json`: `{ "actions": [], "contexts": [] }`                         |
| Input action `Jump`            | `{ "name": "Jump", "type": "bool" }` appended to the source's `actions`                   |
| Input mapping context `OnFoot` | `{ "name": "OnFoot", "priority": 0, "mappings": [] }` appended to the source's `contexts` |

The default folder is `Content/Input` ([IQ-9](#152-open-questions)). A source
groups the actions and contexts that belong together, such as a player's or a
vehicle's; a source of shared actions (`Common.input.json`) holds actions that
other sources map by path.

### 7.2 Asset identity

Input assets follow the [asset identity contract][aid]: a source is a container,
and each product is identified by the source's identity and its own name.

- **Identity.** The source `Content/Gameplay/Player.input.json` has the identity
  `/Content/Gameplay/Player.input`; its action `Jump` is
  `asset:///Content/Gameplay/Player.input/Jump.oiact` and its context `OnFoot`
  is `asset:///Content/Gameplay/Player.input/OnFoot.oimap`. The cooked layout is
  the identity: the descriptor lives at `Gameplay/Player.input/Jump.oiact` under
  the mount's cooked root. A product's source follows from its identity, so the
  editor finds a declaration in that one file; nothing scans folders.
- **References.** Persisted references (script parameter overrides, Play-section
  contexts, scene references) store the identity URI. Inside a source, an action
  of the same source is named by its name; an action of another source by its
  virtual path, `/Content/Input/Common.input/Jump.oiact`, resolved by the cooker
  like every other reference.
- **Uniqueness.** Names are unique per source and asset type, compared after
  Unicode simple case folding (`Jump` and `jump` are refused in one source,
  1.7), so an action `Jump` and a context `Jump` may share a name. Two sources
  never produce one identity, even in one folder; equal names in different
  sources are different assets. A source's file name is part of every identity
  it holds, so renaming the file renames them all (section 8.4).
- **Names.** A name follows the shared segment rule of virtual paths, which the
  schema references as `oxygen.name.schema.json` ([IQ-4](#152-open-questions));
  the cooker and the editor refuse a name with the rule's `vpath.*` code.

| Change                                        | Identity                                                | Referrers rewritten by the relocation                                                            |
| --------------------------------------------- | ------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Rename an action or a context                 | That asset's changes; renamed in its source             | Its source (local names), other sources (paths), scenes, scripts.                                |
| Rename or move a source                       | Every asset it declares changes, as a folder's files do | Other sources (paths), scenes, scripts; local names stay as they are.                            |
| Move an action or a context to another source | That asset's changes                                    | Other sources, scenes, scripts; references across the move switch between local names and paths. |
| Delete an action, a context or a source       | Gone; its outputs retire at the next publication        | None; remaining references show as missing and fail their cook.                                  |
| Duplicate a source                            | The copy's assets are new identities under the copy     | None; the copy's local names refer to the copy's own actions.                                    |

### 7.3 Model

`InputSource` is an immutable typed projection of one file, read and written
only through the engine schemas. Collection items carry session-only element IDs
so that edit sessions, selection and history survive inserts and reorders; IDs
are never written.

```text
InputSource(Identity, Actions[], Contexts[])
InputActionDecl(Id, Name, ValueType: Bool|Axis1D|Axis2D, ConsumesInput)
InputContextDecl(Id, Name, Priority:int32, Mappings[])
ActionRef = LocalName(name) | AssetPath(virtual path)
InputMapping(Id, Action:ActionRef, Slot, Scale:(x,y), Bias:(x,y),
             Triggers[], WrittenAsShorthand)
InputTrigger(Id, Type, Behavior, ActuationThreshold, HoldTime?, TapTime?,
             Interval?, LinkedAction:ActionRef?, RequireHeld, MaxDelay,
             ComboSteps[])
ComboStep(Id, Action:ActionRef, CompletionStates:flags, TimeToComplete)
InputActionDecl and InputContextDecl also carry Description?
```

Reading normalizes nothing that changes the file: a slot alias, the `trigger`
shorthand and omitted defaults are remembered so that an unedited file saves
identically apart from key order and whitespace. Editing an item writes its
canonical form: canonical slot name and the `triggers` array (the shorthand is
kept while the mapping still has exactly that one default trigger).

### 7.4 Fields

Paths and rules are those of the input schemas after M10.2 ([must-fix
findings][mf]); the editor validates with the schema and only adds the
cross-file checks a schema cannot express. Seconds display with an `s` suffix
and three decimals. Defaults are for items the editor creates.

| Field                              | File path                         | New default                        | Validation                                              |
| ---------------------------------- | --------------------------------- | ---------------------------------- | ------------------------------------------------------- |
| Action name                        | `actions[].name`                  | typed at creation                  | section 7.2                                             |
| Description                        | `description`                     | empty                              | up to 256 characters ([IF-13][mf])                      |
| Value type                         | `type`                            | Digital (`bool`)                   | enum                                                    |
| Consume input                      | `consumes_input`                  | off                                | bool                                                    |
| Accumulation                       | `accumulation`                    | Highest absolute                   | enum: `highest_absolute`, `cumulative`                  |
| Context name                       | `contexts[].name`                 | typed at creation                  | section 7.2                                             |
| Priority                           | `contexts[].priority`             | 0                                  | int32; higher runs first                                |
| Input                              | `mappings[].slot`                 | chosen when added                  | a known slot; `None` shows as Unbound                   |
| Scale, bias                        | `mappings[].scale`, `bias`        | (1, 1), (0, 0)                     | finite ([IF-1][mf])                                     |
| Triggers                           | `mappings[].triggers`             | Pressed for Digital, Down for axes | at least one ([IF-3][mf])                               |
| Trigger type                       | `triggers[].type`                 | —                                  | enum; one Action chain type ([IF-10][mf])               |
| Behavior                           | `triggers[].behavior`             | Implicit                           | enum                                                    |
| Actuation threshold                | `triggers[].actuation_threshold`  | 0.5                                | finite, ≥ 0                                             |
| Hold time (Hold, Hold and release) | `hold_time`                       | 0.5 s                              | > 0 ([IF-5][mf])                                        |
| Tap time (Tap)                     | `tap_time`                        | 0.2 s                              | > 0 ([IF-5][mf])                                        |
| Interval (Pulse)                   | `interval`                        | 0.25 s                             | > 0 ([IF-5][mf])                                        |
| Linked action (Action chain)       | `linked_action`                   | must be picked                     | an existing action; not the mapping's own               |
| Require held (Action chain)        | `require_held`                    | off                                | bool                                                    |
| Max delay (Action chain)           | `max_delay`                       | 0 s (no limit)                     | ≥ 0                                                     |
| Combo steps (Combo)                | `combo_steps[]`                   | one step                           | at least one ([IF-13][mf])                              |
| Step action                        | `combo_steps[].action`            | must be picked                     | an existing action                                      |
| Step completes on                  | `combo_steps[].completion_states` | Triggered                          | at least one of Triggered, Completed, Ongoing, Canceled |
| Step window                        | `combo_steps[].time_to_complete`  | 0.5 s                              | > 0 after the first step ([IF-2][mf])                   |

## 8. Commands, Services, Or Adapters

### 8.1 Documents and history

`IInputDocumentService` follows the material document contract
([material editor 8.1, 17, 18](material-editor.md#81-material-commands)): open
by file, edit under the authoring lock, one history entry per committed gesture,
revision before asynchronous work, atomic save with saved-revision
acknowledgement, close drains persistence. The Shell's document service resolves
an external edit (Reload, Keep Mine or Save a Copy) and follows relocations (an
open document re-points its path and its references without closing) for it, as
for every kind.

Opening an action or context row opens its source's document and selects that
asset. A source already open is activated, not opened twice.

### 8.2 Edits

Field edits go through the property pipeline: `PropertyId` is the input schema
kind plus a pointer built from element IDs, resolved to the JSON pointer at
commit. Structural edits are document commands with before and after
`InputSource` snapshots:

| Command                                                   | One history entry                                        |
| --------------------------------------------------------- | -------------------------------------------------------- |
| Add action to context (with first mapping), remove action | Adds or removes all of that action's mappings.           |
| Add, duplicate, remove, reorder mapping                   | Per command; multi-selection is one entry.               |
| Set input (pick or capture)                               | Per binding.                                             |
| Add, remove, reorder trigger; change trigger type         | Type change keeps behavior and threshold, drops others.  |
| Add, remove, reorder combo step                           | Per command.                                             |
| Paste mappings                                            | Appends after the selection; missing actions stay named. |
| Group by action                                           | Makes each action's mappings contiguous (changes order). |

Rename, move and delete are Content Explorer relocations, outside document
history (section 8.4).

### 8.3 Creation

**New ▸ Input ▸ Input Source** asks for a name in place (validated as typed),
writes `<Name>.input.json` through the atomic store, selects its row and opens
its document. **New ▸ Input ▸ Input Action** and **Input Mapping Context** on a
source row, in its document outline and in its context menu ask for a name and
add the declaration to that source as one document edit; on a folder they ask
for the source too, offering the folder's sources and a new one. The action
picker of a context offers **New action**, added to the context's own source by
default, and script `asset` parameter pickers and the Play section's context
picker offer the same commands ([IQ-10](#152-open-questions)). Creating a source
does not cook it; the first Save or a demand from a picker does.

### 8.4 Rename, move, delete

These run as the
[relocation transaction](content-explorer-asset-identity.md#95-asset-relocation)
(journal, atomic writes, rollback, Undo from the result), with the identity rule
of section 7.2. Input sources are referencing sources of the relocation index:
action paths in sources (mapping `action`, `linked_action`, combo step
`action`), context URIs in scene Play sections and scene references, and input
asset URIs in script parameter overrides are rewritten with every other
reference.

- **Rename an action or a context** is a product rename (`InputProductRename`):
  the source renames the declaration and its local references, the old and new
  identities enter the relocation map, and every referrer is rewritten. A taken
  name, also in another case, a missing asset and a name the segment rule
  refuses are rejected before anything is written
  (`OXE.INPUT.Source.ProductNameTaken`, `OXE.INPUT.Source.ProductMissing`,
  `vpath.*`).
- **Rename or move a source** is a file relocation: the source's identity maps
  as a folder, so every action and context it declares moves with it and every
  referrer follows. The confirmation names the count: "Moves 4 input assets".
- **Move an action or a context to another source**, by command or by dragging
  its row onto a source row, removes the declaration from its source and adds it
  to the target in one transaction; its identity changes and its referrers are
  rewritten. References between the moved asset and the assets of either source
  switch form: a local name that now crosses sources becomes a path, and a path
  that now names an action of the same source becomes a local name. A moved
  context takes no actions with it. A name the target already declares rejects
  the move before writing.
- **Delete** lists referrers and confirms when any exist. Deleting an action or
  a context removes its declaration; deleting a source deletes all of its
  assets. A source may be left with no actions and no contexts. Remaining
  references show as missing and fail their cook.
- **Duplicate a source** copies it under a unique name
  (`Player (2).input.json`); its assets are new identities and its local names
  refer to its own actions.

### 8.5 Key capture

`KeyCaptureSession` is a view-model object that consumes abstract events and
yields a slot or nothing; the view feeds it from routed events of its capture
surface. Tests drive it with synthesized events, without real input, focus or a
window.

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
| Key with no engine slot               | Stays in capture and shows "This key has no Oxygen input" (`OXE.INPUT.Capture.Unsupported`). |

Mouse movement and wheel axes (`MouseX`, `MouseXY`, `MouseWheelY`...) are picked
from the list: motion is too ambiguous to capture. Capture reads only events
routed to the editor window; it installs no hooks.

While capturing, the session owns the keyboard:

- **Editor accelerators are swallowed.** The capture surface handles key events
  in the preview (tunnelling) phase and marks every one handled, so Ctrl+S,
  Ctrl+Z, Delete and the Play and Stop shortcuts never reach the command system.
  Capture completes on the first key down, and the surface keeps swallowing key
  events until every key pressed during capture is released: pressing Ctrl+S
  binds Left Ctrl and does not save.
- **System keys are captured.** Alt, F10 and Tab bind `LeftAlt`/`RightAlt`,
  `F10` and `Tab`; they do not activate the menu bar or move focus.
- **One key per binding.** A modifier binds at once, as any other key. The
  flyout says "For Shift + W, add an Action chain on W linked to a Shift
  action", because combinations are triggers, not slots.

The session reports, for each event, whether the view must mark it handled;
tests assert that report, so accelerator and system-key behavior is tested
headlessly with the rest.

### 8.6 Validation service

`InputValidation` runs on load, after every commit and before cook. The source
reader checks JSON, the schema, duplicate names and undeclared local names, each
at its position; `InputRules.CheckActionPaths` (Input) checks that every
action path names an action a project source declares (a path into a cooked library is
left to the cook); the editor adds input overlaps (section 9.3). Results attach
to fields and rows; they do not block editing. Errors block that source's cook
with the same messages.

## 9. UI Surfaces

### 9.1 Content Explorer

- **Rows.** An input source is a container row, shown like a folder: it expands
  to its actions and contexts, each with its own row, and opening the source
  opens its document. Rename, Move and drag on an action or context row act on
  that asset (section 8.4); on the source row they act on the source and all of
  its assets. Reveal source on an asset row opens the source at the declaration.
  A source that is not JSON or fails the schema shows as Broken, with the
  reader's messages and **Open in code editor**; its cooked assets keep their
  rows until the next publication.
- **Tiles.** Generated, no renderer: the action tile shows its value-type glyph
  (button, slider, two-axis pad) and the consume-input mark; the context tile
  shows up to four key caps of its first bindings and the mapping count. Both
  kinds keep the input brush; the glyphs distinguish them.
- **New ▸ Input** menu with the two creation commands; the context menu mirrors
  it. **Open in code editor** stays available on every input row.
- **Details.** Action: value type, consume input, its source, and "Used by":
  contexts with their bindings (`Gameplay — Space, Pressed`), scripts and
  scenes, each opening its user. Context: priority, mapping and action counts,
  its source, and scenes and scripts that start or switch it. Source: its
  actions and contexts, and the sources whose mappings use its actions.
- **Filters.** The existing Input actions and Input mapping contexts filters
  include source rows.

### 9.2 Action editor

```text
+-----------------------------------------------------------------------+
| [glyph] Jump      Input action · Content/Input/Player.input            |
|                                                    [Needs cooking]    |
+-----------------------------------------------------------------------+
| Description    [ Makes the player jump                             ]  |
| Value type     [ Digital | Axis 1D | Axis 2D ]                         |
| Consume input  [x]  When it triggers, its input is not delivered to   |
|                     later mappings in its context or to lower-priority |
|                     contexts.                                          |
+-----------------------------------------------------------------------+
| Used by                                                                |
|   Gameplay   Space · Pressed                         [Open]            |
|   Vehicle    Space · Pressed, Hold 0.400 s           [Open]            |
|   Scripts    player.lua · jump_action                [Open]            |
+-----------------------------------------------------------------------+
```

Changing the value type of a used action shows, inline, what the engine does
with each binding (a key on an axis action drives X with 1 while pressed); it is
not blocked.

### 9.3 Context editor

```text
+------------------------------------------------------------------------------+
| [glyph] Gameplay  Input mapping context · Content/Input/Player.input          |
| Priority [ 100 ]                                                    [Cooked]  |
+--------------------------------------+---------------------------------------+
| Mappings · evaluation order [+Action] | Jump · Space                          |
| ▾ Jump            Digital   [+] [⋯]  | Input   [ Space        ] [⌨ Press]    |
|     [Space]  Pressed    ⚠ also Leap  | Scale   X [1.0] Y [1.0]               |
|     [F]      Pressed · Hold          | Bias    X [0.0] Y [0.0]               |
| ▾ MoveForward     Axis 1D   [+] [⋯]  | Triggers                    [+ Add ▾] |
|     [W]      Down                    |  ▾ Pressed [Implicit|Explicit|Blocker]|
|     [Up]     Down                    |     Actuation threshold  0.50         |
| ▾ Look            Axis 2D   [+] [⋯]  |  ▾ Hold    [Implicit|Explicit|Blocker]|
|     [Mouse XY] ⚠ Needs a trigger     |     Hold time            0.400 s      |
+--------------------------------------+---------------------------------------+
```

- **Header.** Priority (higher runs first; the Play section starts with this
  value unless it overrides it). Contexts start only from a scene's Play section
  ([IQ-6](#152-open-questions)).
- **Mappings list.** Evaluation order, top to bottom, because order decides
  which mapping consumes input first. Consecutive mappings of one action form a
  group with the action's name and value type; the group header adds a binding
  (`+`), opens the action, removes the action's mappings or moves the group. An
  action whose mappings are not contiguous appears in several groups, and
  **Group by action** in the list menu joins them; it warns first only when the
  regrouping changes which mapping consumes an input first. New bindings join
  their action's last group. Drag reorders rows and groups.
- **Rows.** A key cap (click opens the key selector), then trigger chips, then a
  badge for errors and warnings. A missing action shows its name in error style
  with **Pick action** and **Remove mappings**.
- **Input overlap rule.** Order changes behavior only between mappings whose
  inputs overlap, so the editor shows it exactly there, with one check that
  serves both purposes. Two mappings overlap when their slots share a physical
  source, by the rule of [IF-9][mf] (`Space` with `Space`, `MouseXY` with
  `MouseX`, `MouseWheelY` with `MouseWheelUp`, `AnyKey` with every key). The
  check covers mappings in this context and in every context that a scene's Play
  section starts together with it, in evaluation order: higher priority first,
  then Play-section order, then mapping order. For each overlapping pair, the
  later row shows one non-blocking warning:
  - if the earlier mapping's action consumes input, the later one is shadowed:
    "Space is consumed first by Jump" (`OXE.INPUT.Mapping.InputShadowed`);
  - otherwise both can fire: "Space · also Jump in Gameplay"
    (`OXE.INPUT.Mapping.BindingConflict`).

  The key selector shows the same message for a slot before it is chosen.

- **+ Action** opens the action picker (search, value type, source; New action);
  actions of this source come first, then those of other sources, which the
  mapping names by path. Choosing adds the action with one binding that opens
  the key selector.
- **Details pane.** The selected binding's input, then Scale X/Y and Bias X/Y,
  then its triggers. Triggers are an ordered list of cards: a type dropdown, a
  three-way behavior selector, remove and move. Each type shows only the fields
  the engine reads (table 7.4). Combo shows its steps as a table: action picker,
  "completes on" flag chips, window in seconds, move and remove. Action chain
  shows the linked action, Require held (the linked action must be held at the
  press) and Max delay (0 for no limit). Tooltips state the engine's meaning:
  Implicit triggers must all trigger; with any Explicit trigger, at least one
  Explicit must trigger; a triggered Blocker stops the mapping.
- **Multi-selection.** Several rows: Delete, Duplicate, Copy, Move up/down, and
  scale and bias with mixed values. Inputs and triggers edit one row at a time;
  the pane says "Select one binding to edit its input and triggers".
- **Outline.** The document's left outline lists the source's actions and
  contexts with **+ Action** and **+ Context**; selecting one shows its editor,
  and the outline's rows rename, delete and drag like their Content Explorer
  rows.
- **Clipboard.** Copy and paste of bindings between contexts as their schema
  JSON; pasting into another source turns local action names into the actions'
  paths (`/Content/Input/Player.input/Jump.oiact`), and paths that name the
  target's own actions into local names.

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
both. Each entry shows what the engine sends: a button value, a 1D delta or a 2D
delta, and a slot that would overlap an existing mapping carries the input
overlap message ("Space is consumed first by Jump" or "also Jump in Gameplay",
section 9.3). Pressing **Press a key** starts the capture session (section 8.5);
the button reads "Press a key… Esc to cancel" while waiting, and the flyout
shows the one-key-per-binding hint.

### 9.5 State and diagnostics

The document header shows the shared cook state chip (Needs cooking, Out of
date, Cooked, Failed) as in the material editor. Field errors show inline; the
list badge and the row's tooltip carry the first message; the cooking panel
lists cook failures with Go To Property.

## 10. Persistence And Round Trip

- The writer emits schema-valid JSON, 2-space indentation, keys in schema order,
  `$schema` kept when present.
- An unedited file reopens and saves to the same values; an edited item is
  written canonically (section 7.3). The four example input sources under
  `Examples/Content` round-trip with identical cooked descriptors.
- Element IDs, selection, outline state and capture state are not persisted.
- Unresolved action references stay as authored.
- Saving never cooks synchronously and never saves other documents.

## 11. Live Sync / Cook / Runtime Behavior

- **Trigger.** Successful Save queues an incremental cook of that file's job
  ([cooking workflows §4](content-cooking-workflows.md#4-cooking-triggers));
  picker demand cooks the saved closure. No live projection: editor viewports do
  not run game input.
- **Jobs.** One `input` job per source, named by the source's identity (section
  7.2). Its path references give `depends_on` on the sources that declare them
  and, for actions of cooked libraries, the `cooked_context_roots` in content
  priority order, as for any descriptor reference. A changed source makes the
  sources, scenes and scripts that use its assets stale.
- **Outputs.** An input job has several outputs. Provenance records them under
  the source; after a rename, a move or a removal, outputs the source no longer
  declares are retired with the existing retirement path, so old names leave the
  index.
- **Failures.** The cooker's `input.*` diagnostics keep their codes and map to
  properties through their object path, a `$`-rooted JSON path after
  [IF-14][mf]. A dirty input document in a cook closure follows the Save listed
  and Cook flow.
- **Play.** The Play section's contexts and script references resolve to the
  cooked assets; the player hydrates them and identifies them at runtime by key.

## 12. Operation Results And Diagnostics

Operations: `Input.CreateSource`, `Input.Create`, `Input.Open`, `Input.Edit`,
`Input.Save`, `Input.Cook`, `Input.Rename`, `Input.Move`, `Input.Delete`,
`Input.Duplicate`. Domain `InputAuthoring`; persistence uses `Document`, cooking
`AssetCook`, relocation its existing domain.

| Code                                    | Severity | When                                                                                                                    |
| --------------------------------------- | -------- | ----------------------------------------------------------------------------------------------------------------------- |
| `OXE.INPUT.Source.NotJson`              | Error    | The source is not JSON; the problem has its line and column; editing is disabled.                                       |
| `OXE.INPUT.Source.SchemaMismatch`       | Error    | The source fails the engine schema, at the value the schema refuses; editing is disabled.                               |
| `OXE.INPUT.Source.ActionDuplicate`      | Error    | The source declares two actions with one name.                                                                          |
| `OXE.INPUT.Source.ContextDuplicate`     | Error    | The source declares two contexts with one name.                                                                         |
| `OXE.INPUT.Source.ActionUndeclared`     | Error    | A mapping, linked action or combo step names, by its name, an action the source does not declare.                       |
| `OXE.INPUT.Source.ActionMissing`        | Error    | A reference names, by its path, no action of a project source.                                                          |
| `OXE.INPUT.Source.ProductMissing`       | Error    | A rename or move names an action or context the source does not declare.                                                |
| `OXE.INPUT.Source.ProductNameTaken`     | Error    | A create, rename or move gives a name the source already declares for that type, also in another case.                  |
| `vpath.*`                               | Error    | A name the segment rule refuses (empty, reserved character, too long, ...).                                             |
| `OXE.INPUT.Source.SlotUnknown`          | Error    | A slot name the engine does not know.                                                                                   |
| `OXE.INPUT.Source.TriggerFieldRequired` | Error    | A schema-required trigger field is missing or out of range.                                                             |
| `OXE.INPUT.Source.ComboStepUnreachable` | Error    | A step with no completion state, or a zero window after the first step.                                                 |
| `OXE.INPUT.Source.ValueOutOfRange`      | Error    | Non-finite or out-of-range number; the edit is rejected.                                                                |
| `OXE.INPUT.Mapping.BindingConflict`     | Warning  | An overlapping input is bound by an earlier mapping that does not consume; both fire (input overlap rule, section 9.3). |
| `OXE.INPUT.Mapping.InputShadowed`       | Warning  | An earlier overlapping mapping consumes the input, so this one never sees it (input overlap rule).                      |
| `OXE.INPUT.Capture.Unsupported`         | Info     | A captured key has no engine slot.                                                                                      |
| `OXE.INPUT.Source.CreateFailed`         | Error    | Creating a source failed; names the path and cause.                                                                     |

The first eight are reported by the source reader, the writer and
`InputSourceFiles` today; the rest come with the input editor.

Diagnostics carry the document, file path, asset URI and property path. Engine
`input.*` codes stay as reported.

## 13. Dependency Rules

- InputEditor depends on Documents, Schemas, Managed.Assets, ContentExplorer
  pickers and ContentPipeline services; nothing depends on InputEditor except
  the app's composition root.
- Managed.Assets owns the reader and writer; ContentPipeline owns finding a
  product's source (`InputSourceFiles`); ContentExplorer and the input editor
  consume them rather than parsing input sources themselves.
- Key translation exists once (Runtime); WorldEditor's viewport bridge and key
  capture both use it.
- No editor code reads `.oiact`/`.oimap` bytes; cooked facts come from the loose
  index.

## 14. Validation Gates

| ID    | Check                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IA-1  | Creating a source, then an action and a context in it, writes a schema-valid source whose cook produces `<source>/<name>.oiact` and `.oimap` with the expected keys; reader and writer tests cover every field and default.                                                                                                                                                                                                            |
| IA-2  | The example input sources open, save unchanged in value and cook to identical descriptors; aliases and shorthand survive until edited.                                                                                                                                                                                                                                                                                                 |
| IA-3  | Key capture session tests with synthesized events: letters, digits, both-side modifiers, keypad and extended keys, mouse buttons, wheel, Escape, focus loss; Ctrl+S, Ctrl+Z, Delete and the Play and Stop shortcuts are swallowed and bind their first key; Alt, F10 and Tab are captured, not acted on.                                                                                                                               |
| IA-4  | Every trigger type's fields edit, validate and round-trip; type change, reorder, combo steps and flags undo and redo as single entries.                                                                                                                                                                                                                                                                                                |
| IA-10 | Input overlap rule, one implementation: an identical slot twice in a context, an overlapping pair (`MouseXY` then `MouseX`), and contexts started together by a Play section at different and equal priorities each report `InputShadowed` when the earlier action consumes and `BindingConflict` when it does not, on the later row and in the key selector; Group by action warns only when it changes which mapping consumes first. |
| IA-11 | Renaming an action rewrites its declaration, its local references in its source, the paths to it in other sources, Play sections and script overrides; Undo restores every file. A taken name in another case is rejected before writing.                                                                                                                                                                                              |
| IA-12 | Renaming and moving a source moves every asset it declares, and every reference to them follows; the confirmation names the count.                                                                                                                                                                                                                                                                                                     |
| IA-13 | Moving an action between sources rewrites its referrers and switches references across the move between local names and paths on both sides; a name the target declares rejects it before writing.                                                                                                                                                                                                                                     |
| IA-14 | Every emitted key maps to its source: Reveal source and Go To Property from a cooked row open the declaration; an asset removed by rename, delete, move, hand edit or external removal of its source leaves the index at the next publication.                                                                                                                                                                                         |
| IA-5  | Two sources in one folder declaring `Jump` show two `Jump` rows under their sources and cook two keys; a context mapping the other source's `Jump` by path resolves only that one.                                                                                                                                                                                                                                                     |
| IA-6  | Gesture, undo/redo, dirty, save-then-edit and external-change conflict behavior match the material document contract.                                                                                                                                                                                                                                                                                                                  |
| IA-7  | Save cooks the source and its dependents; an action referenced by path from another source, folder and mount resolves, and the referenced source cooks first; a renamed action's old output leaves the index; cook errors navigate to the field.                                                                                                                                                                                       |
| IA-8  | A scene whose Play section starts an editor-authored context runs in the player, and a script's `on_action` handler fires for a bound key.                                                                                                                                                                                                                                                                                             |
| IA-9  | Owner review of SC1–SC10 in the running editor.                                                                                                                                                                                                                                                                                                                                                                                        |

## 15. Open Issues

### 15.1 Engine gaps

Capabilities the engine does not provide, which this design therefore does not
offer. Engine defects and inconsistencies found during the analysis are not
gaps: they are fixed before the input UI is built, as listed with their
pass/fail checks in the plan's [findings][findings].

| ID   | Gap                                                                                                                                                                                  | Editor handling                                       | Matters for V0.1                                                                                                    |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| EG-1 | Runtime trigger options absent from the cooked format: hold one-shot; pulse trigger-on-start, limit, jitter, phase, ramp; combo breakers (chain options join the format with IF-10). | Not offered.                                          | No; combo breakers and hold one-shot are the first candidates after V0.1.                                           |
| EG-2 | No input modifiers beyond scale and bias (dead zone, smoothing, response curves).                                                                                                    | Not offered.                                          | No; negate is a negative scale, and a scalar input feeds both components, so swizzle is not needed for keys (IF-1). |
| EG-3 | No triggers on the action itself shared by all its mappings.                                                                                                                         | Not offered; per-binding triggers only.               | No.                                                                                                                 |
| EG-4 | No 3D axis value type.                                                                                                                                                               | Not offered.                                          | No.                                                                                                                 |
| EG-5 | No gamepad slots.                                                                                                                                                                    | Not offered; the key selector has no gamepad section. | No (out of scope).                                                                                                  |
| EG-6 | No player-remappable bindings or runtime rebinding.                                                                                                                                  | Not offered.                                          | No (out of scope).                                                                                                  |
| EG-7 | No API to evaluate a context against synthetic input outside a running game.                                                                                                         | No in-editor preview (IQ-7).                          | No.                                                                                                                 |

### 15.2 Open questions

In priority order. Decisions come from the ED-M08 owner's review on the pull
request; the user interface items await the owner at ED-M10.1.

| ID    | Question                                          | Recommendation                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ----- | ------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IQ-1  | Authoring format and file granularity.            | Decided 2026-10-11, revised 2026-10-19 with the asset identity contract: the engine format unchanged; an input source is a container of actions and contexts, created by the editor and edited in place; actions and contexts are added to a source (sections 7.1, 8.3).                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| IQ-2  | Scale and bias have no runtime effect (IF-1).     | Decided 2026-10-11 (review): fixed before the UI ([IF-1][mf]); Scale X/Y and Bias X/Y shown per binding.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| IQ-3  | Input asset identity and name scope.              | Decided: an asset is named under its source ([asset identity][aid] 1.2); names are unique per source and type; equal names in different sources are distinct (section 7.2).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| IQ-4  | Name pattern.                                     | Decided: the shared segment rule of virtual paths (`oxygen.name.schema.json`), enforced by the schema and the cooker.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| IQ-5  | Chord and action chain.                           | Decided 2026-10-11 (review): one Action chain type with Require held and Max delay; `chord` removed from the format ([IF-10][mf]).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| IQ-6  | Load when mounted and Activate when loaded flags. | Decided 2026-10-11 (owner): removed from the engine (ED-M08.10l); scenes' Play sections are the only way contexts start.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| IQ-7  | In-editor live preview of a context.              | Decided 2026-10-11 (review): deferred past V0.1; Play is the test path (EG-7).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| IQ-8  | Module placement.                                 | Decided 2026-10-11 (review): new `Oxygen.Editor.InputEditor` project beside MaterialEditor; ARCHITECTURE and project layout list it.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| IQ-9  | Default folder.                                   | Decided 2026-10-11 (review): `Content/Input`, created by the project template and used by the creation target policy ([project layout](project-layout-and-templates.md)).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| IQ-10 | Text needed in documents this LLD does not own.   | Decided 2026-10-11 (review): the ED-M08 owner amends the documents they own; this LLD needs: [script authoring](script-authoring.md) §4 says Edit on an input asset opens the input editor and Open in code editor remains, and §6 and §8 say the input action and context pickers offer New action and New context and the Play section's priority defaults to the context's own (M10.9 behavior, added when M10.1 closes); Cross-source references by path are enabled and `OXE.SCRIPT.INPUT_REFERENCE_UNSUPPORTED` is retired. [Content explorer §9.5](content-explorer-asset-identity.md#95-asset-relocation) and [content pipeline §24](content-pipeline.md#24-input-sources) state the container identity, references, renames and moves. |
| IQ-11 | Mapping order.                                    | Decided 2026-10-11 (owner): evaluation order with contiguous action groups and an explicit Group by action, plus the input overlap rule that marks where order changes behavior (section 9.3).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| IQ-13 | Moving one asset.                                 | Decided 2026-10-11 (owner, decision 9), kept: Move and drag on an asset row move that asset, now to another source (section 8.4); moving the source row moves the source with all its assets. Dragging one row moves only what the row shows.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| IQ-12 | Schema augmentation (IF-2 to IF-6).               | Decided 2026-10-11 (review): part of M10.2 ([IF-2 to IF-6][mf]).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

### 15.3 Rejected alternatives

- **Editor-owned input format** (per-asset files with GUID references): a
  parallel schema the cooker cannot read and a second identity scheme beside the
  engine's names.
- **One project-wide input source edited as one document**: one undo stack and
  one conflict surface for unrelated contexts, and no per-asset rows.
- **Hand-written files read-only in the editor**: two workflows for the same
  assets; scripting already makes hand-written files first-class.
- **Identity from the declared name alone**, every input asset in one `Input`
  folder: identity would not follow the source location as for every other
  asset, moves would bypass relocation, and names would have to be unique
  project-wide.
- **Identity from the source's folder and the declared name** (the first
  revision of this design): two sources in one folder declaring `Jump` collide,
  moving a source changes its assets' identities without the relocation knowing
  them, and it needed a single-asset file form, a stem rule, Split and a file
  rename that changes no identity to stay predictable.
- **One asset per file** (`*.input-action.json`): a second format and schema for
  what an input source already says, and a file per action for content that
  belongs together.
- **GUID references inside input sources**: the engine and every other
  descriptor reference assets by virtual path.
- **An editor simulation of triggers for preview**: a second implementation of
  engine semantics that would drift.
- **A grouped mapping view that hides order**: order decides consumption.
- **Capture through global keyboard hooks**: needs real input and focus, cannot
  be tested headlessly and reaches outside the editor.

[mf]: ../plan/ED-M10-input-authoring.md#must-fix-before-the-input-ui
[findings]: ../plan/ED-M10-input-authoring.md#findings-from-the-analysis
[input-cooking]: ../../../projects/Oxygen.Engine/design/content-pipeline/input-cooking-architecture.md
[player-start]: ../../../projects/Oxygen.Engine/design/runtime-core/scene-instantiation-and-player.md#scene-start-state
[aid]: asset-identity.md#5-target-design
