# Script Authoring LLD

Status: `design; decisions SD-1–SD-7 (2026-10-11) and SD-8–SD-12 (2026-10-13)`;
delivered by
[ED-M08.10](../plan/ED-M08-runtime-parity-and-standalone-validation.md#m0810--engine-capability-parity-in-the-editor).

Summary: users write Luau scripts in their own code editor and do everything
else in the Oxygen editor: create a script from a template, attach it to nodes,
set its parameters with typed fields and pickers, see its errors as they save,
and press Play to run the cooked scene with live script reload. The engine owns
the script model, parameter declarations, diagnostics and the player
([scene script hosting](../../../projects/Oxygen.Engine/design/scripting/scene-script-hosting.md),
[script diagnostics](../../../projects/Oxygen.Engine/design/scripting/script-diagnostics.md),
[player host](../../../projects/Oxygen.Engine/design/runtime-core/scene-instantiation-and-player.md));
this LLD owns the authoring workflow. Physics bindings are out of scope.

## 1. Scenarios

| ID  | Scenario                                                                                                                             |
| --- | ------------------------------------------------------------------------------------------------------------------------------------ |
| SC1 | Create a script from the Content Browser; it opens in the user's code editor.                                                        |
| SC2 | Declare parameters in the script; after saving, the inspector shows one field per parameter.                                         |
| SC3 | Add a Scripting component to a node; add, remove and reorder slots; pick a script per slot.                                          |
| SC4 | Set parameter values: numbers with ranges and units, vectors and colors, choices, and references to assets, input actions and nodes. |
| SC5 | See script errors on save, in the inspector, on the script in the Content Browser and in Problems; open the code editor at the line. |
| SC6 | Save, cook and reopen keep slots and values; renaming or moving a referenced asset or node keeps the reference.                      |
| SC7 | Press Play: the scene runs in the player with scripts and input; logs and errors stream back; editing a script reloads it live.      |
| SC8 | Stop, change something, and Play again.                                                                                              |

## 2. Traceability and links

REQ-005/006/007/017/043/044; GOAL-001/006. Architecture: the editor owns
authoring, the engine owns execution, Interop exposes the analyzer
([RULES](../../oxygen/RULES.md#ownership)). Related LLDs:
[property inspector](property-inspector.md),
[property pipeline](property-pipeline.md),
[scene authoring model](scene-authoring-model.md),
[content browser identity](content-browser-asset-identity.md),
[content pipeline](content-pipeline.md), [cooking panel](cooking-panel.md),
[diagnostics and operation results](diagnostics-operation-results.md),
[settings](settings-architecture.md).

## 3. Baseline

- Scenes carry scene-level script references (`SceneReferencesData.Scripts`)
  with typed pickers. They are cook dependencies for scripts loaded at runtime
  by other scripts; nothing executes them. They stay as they are.
- The Content Browser knows the Script kind for cooked `.oscript` outputs only;
  `Content/Scripts` exists in new projects.
- The editor cannot create input actions or mapping contexts and does not cook
  input files. Editor input authoring belongs to
  [ED-M09](../plan/ED-M09-input-authoring.md); this design cooks hand-written
  input files (section 4, Input files).
- There is no node scripting component, no script cook job and no way to run a
  scene from the editor. Editor viewports do not run scripts; that does not
  change in V0.1 (in-editor simulation is post-V0.1).

## 4. Script assets

**Source recognition.** `.lua` and `.luau` files under authoring mounts are
Script assets in the source state; their cooked `.oscript` is the cooked state
of the same identity.

**Identity.** A script is identified by its source location, like every other
asset: the asset name is the file stem and the script job's layout places the
output in the source file's folder, so `Content/Player/move.lua` cooks to
`/Content/Player/move.oscript`, not to a shared `Scripts` folder. One file holds
one script, so renaming or moving the file is the ordinary relocation and
rewrites the slots and overrides that refer to it. Two sources with the same
stem in one folder (`move.lua` and `move.luau`) would produce the same virtual
path; both show the error `OXE.SCRIPT.IDENTITY_COLLISION` and neither cooks
until one is renamed. New Script, rename and move reject a name that would
collide before writing.

**New Script** (Content Browser New menu, beside New Material; SD-9): a dialog
asks for a name and a folder, prefilled with a free name and the folder being
browsed (`Content/Scripts` at a mount root). It writes `<Name>.lua`, selects it
and opens it in the code editor. The template:

```lua
local Script = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1, min = 0, unit = "m/s",
      description = "How fast the node moves" },
  },
}

function Script:on_start()
end

function Script:on_gameplay(dt)
end

return Script
```

**Edit Script**: double-click or the Edit command opens the file in the
configured code editor, at a line and column when invoked from a diagnostic. The
choices are VS Code (`code --goto file:line:column`), the system default for the
file type, and a custom command with `{file}`, `{line}` and `{column}`
placeholders. The first Edit asks which one to use, with VS Code preselected
when `code` is found and **Remember my choice** checked; the choice is a
per-user editor setting. **Open with** on the Edit button changes it later
(SD-8). A failed launch is an operation result
(`OXE.SCRIPT.EDITOR_LAUNCH_FAILED`) naming the command.

**Details pane**: the declared parameters (name, type, default) and the
diagnostic counts, read from analysis (section 5). Rename, move and delete
follow the existing transactions and retarget slot references.

**Input files**: users write the cooker's input files by hand (`*.input.json`
for mapping contexts with their actions, `*.input-action.json` for single
actions, validated by `oxygen.input.schema.json`). **New Input File** in the New
menu writes `<Name>.input.json` with its `$schema` set, one sample action and
one sample context, and opens it in the code editor. Files under authoring
mounts are Input sources, shown as one Input tile; the cook emits one `input`
job per file, and each declared action and mapping context then appears as its
own tile in the file's folder (SD-12) and in asset and Play-section pickers. The
details pane of an action or context names its file. Edit on the file or on any
of its assets opens the file in the code editor (the input editor once
[ED-M09](../plan/ED-M09-input-authoring.md) delivers it, with **Open in code
editor** kept), and schema or import errors fail the cook in the cooking panel.
Input assets are identified by their source location, like every other asset
([input authoring §7.2](input-authoring.md#72-asset-identity)): each job's
layout places its outputs in the source file's folder, so `Jump` declared in
`Content/Gameplay/Player.input.json` is `/Content/Gameplay/Jump.oiact`. Until
the engine resolves references between input files by virtual path
([ED-M09](../plan/ED-M09-input-authoring.md) IF-12), only self-contained files
cook: a mapping, chain or combo step that names an action the file does not
declare fails that file's job before the cooker runs
(`OXE.SCRIPT.INPUT_REFERENCE_UNSUPPORTED`), so the result never depends on job
order. ED-M09.4 retires that code when it enables references by virtual path.

**Project tooling** (SD-11): projects contain `.luaurc`, `Types/oxygen.d.luau`
copied from the SDK and `.vscode/settings.json` pointing `luau-lsp` at those
definitions, so VS Code completes the `oxygen` API and reports the same type
errors the engine reports. `.luaurc` cannot name definition files; the VS Code
setting does. Opening a project creates missing files, refreshes
`Types/oxygen.d.luau` when the SDK's copy differs, and adds only missing keys to
an existing `.vscode/settings.json`, never overwriting a value. Each change is
logged to Output.

## 5. Analysis

`IScriptAnalysisService` (WorldEditor) calls Interop's
`ScriptAnalysis.Analyze(sourceName, bytes, level)`, which wraps the engine
`ScriptAnalyzer`, and returns declarations with hints plus diagnostics.

- Results are cached by path and content hash.
- The project catalog's change notifications re-analyze a script when its file
  changes, so the inspector and Problems update on save. Analysis is not
  cooking; the cook still runs only on its existing triggers.
- Level `kTypes` when the type definitions are available, otherwise `kLint`.

## 6. Node scripting component

### Data

| Field                       | Type                         | Notes                                                |
| --------------------------- | ---------------------------- | ---------------------------------------------------- |
| `Slots`                     | ordered list of `ScriptSlot` | List order is the slot position.                     |
| `ScriptSlot.Id`             | GUID                         | Unique within its node; commands and cook `slot_id`. |
| `ScriptSlot.Script`         | script identity (`.oscript`) | Missing references stay visible.                     |
| `ScriptSlot.ExecutionOrder` | int32, default 0             | Lower runs earlier, across all nodes.                |
| `ScriptSlot.Overrides`      | map key → typed value        | Only values that differ from the declared default.   |

Override values are `bool`, `int32`, `float`, `string`, `vec2`–`vec4`, an asset
reference (asset URI, retargeted like every other asset reference) or a node
reference (node identity in the document, converted to a node index at cook). A
slot refers to a script by the asset it cooks to, so renaming or moving the
`.lua` file rewrites the slots and asset values that use it.

### Copies

Duplicate and paste copy slots with their identities, scripts, execution order
and values; a slot identity is unique within its node, as in the engine and the
cooked `node_index`/`slot_id` pair. Each operation maps every copied node to its
copy, so a node value that refers to a node copied in the same operation (a
turret's own barrel, or another copied root) refers to that copy. Other node
values are kept: in the same scene they still name the original node; pasted
into another scene they show as missing and the cook reports them until a node
is picked. Cut and paste moves nodes without new identities, so their references
are unchanged.

### Inspector

A **Scripting** section, added through Add Component, shown for a single
selected node (multi-selection shows a message to select one node).

- One card per slot, in order: a script picker (filtered to Script assets, with
  New Script, which runs the Content Browser's New Script and picks the new
  script), Edit in the code editor, the execution order field, move up and down,
  and remove. Add Component starts the section with one empty slot. The card
  header shows the script name and, when the script has problems, a badge with
  the error and warning counts; its tooltip lists the problems, and clicking it
  opens Problems at the first one. Edit is unavailable, with the reason as its
  tooltip, while the script has no known source.
- Below the picker, one field per declared parameter, using the declaration
  hints: number fields with min, max, step and unit; vector fields; a color
  field for `color = true`; a choice list for `options`; a text box with the
  string length limit; an asset picker filtered by `kind` (input actions and
  mapping contexts included, offering New action and New context once ED-M09.9
  delivers them); and a node picker with search that also accepts a node dragged
  from the Scene Explorer. Labels come from the parameter name, with the
  description as tooltip; dragging a number's label, or a vector component's
  label, scrubs the value.
- An overridden value shows the override marker and a reset to the default.
  Setting a value equal to the declared default removes the override, so a later
  change of the default applies. Numbers are clamped to the declared range, and
  strings hold at most 59 UTF-8 bytes, the cooked record's limit.
- Overrides whose key the script no longer declares, or whose type changed, are
  listed under "Not declared by the script": one row per value with its name,
  type and stored value, and its own remove button; Remove all appears for two
  or more. They are kept until removed, and the cook reports them (section 7).
  With no declared parameters the card says so, and the group still lists the
  values.
- While the script has errors, its last valid declarations stay on screen,
  marked as out of date.
- A script from a mounted cooked library has no source in the project. Its
  parameters come from its cooked descriptor, read by the native Inspector's
  `scripts` command; the descriptor keeps names, types, defaults and asset kinds
  but no range, unit, description, options or color, so the card says so and
  those fields have no limits or hints.

All edits are commands with undo/redo and dirty state through the property
pipeline. The component has no live projection: scripts do not run in editor
viewports.

### Persistence

The component serializes with the scene as `$type: Scripting`; each value is
stored with its type, as `{ "Type": "float", "Value": 2.5 }`. Scene files carry
no format version, and an editor without the component refuses to load a scene
that has one. Save and reopen keep slot identity, order, execution order and
overrides.

## 7. Cooking

- Every script referenced by a slot or by the scene references gets a `script`
  job with its output at the source folder (section 4): compiled bytecode (debug
  mode) embedded, with the source path recorded relative to its authoring mount
  folder so the player can reload it
  ([hosting §6](../../../projects/Oxygen.Engine/design/scripting/scene-script-hosting.md#6-source-reload)).
  The scene depends on the scripts and asset values of its slots, so a scene
  cook includes them.
- Every input file under an authoring mount gets an `input` job with its outputs
  at the source folder (section 4); scenes and sidecars that reference its
  actions or contexts depend on it.
- Every scene with scripted nodes gets one `script-sidecar` job generated from
  the scene model, with inline bindings: node index from the descriptor
  generator, `slot_id` from the slot GUID, execution order, and typed overrides
  (asset as virtual path, node as index). It depends on the scene and script
  jobs, cooks only with its scene, and resolves scripts and asset values reused
  from earlier cooks through the published roots.
- A slot without a script is left out of the cooked scene with a warning. A slot
  whose script no longer exists and a node value whose node the scene no longer
  has fail the scene before the cooker runs.
- Compile errors, stale overrides, missing nodes and wrong asset kinds fail the
  cook with diagnostics in the cooking panel, rendered with their spans, with Go
  To Property for slot and override errors: it opens the scene and selects the
  node, whose inspector shows the slot.

## 8. Play

Three rules shape Play: the scene says how the game starts; Play runs exactly
that, in the player that ships; and the editor always says whether the running
game shows the latest edits.

### Play section of the scene

Settings that decide how the scene starts as a game are properties of the scene,
not of its nodes, as in Unreal's World Settings. The scene inspector gains a
**Play** section beside the environment sections.

| Field                   | Source/default                                                  | UI/effect                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| ----------------------- | --------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Starting camera         | Camera node reference in the scene; unset                       | A list of Automatic and the scene's cameras by name. With Automatic, the description reads "Resolves to _name_ (first in hierarchy)", the name selecting that camera in the Scene Explorer; a deleted camera replaces it with the error. Cooked as `starting_camera`.                                                                                                                                                                                                |
| Starting input contexts | List of mapping context references, each with a priority; empty | "Add" lists the contexts not yet started. The contexts show highest priority first, the order the engine evaluates them; equal priorities keep the order they were added in. Each row has its name, which shows the context in the Content Browser, its priority, shown as the context's own until overridden, with a reset while overridden, and Remove; a context menu repeats them. A missing context shows an error icon and "Missing", and can only be removed. |

- Higher priority wins; equal priorities resolve in list order. The cooked
  `starting_input_contexts` always carries the effective priority.
- An empty list shows: "No input context starts. Input does nothing until a
  script activates a context."
- Each camera section carries a **Starting camera** toggle that edits the same
  scene reference: turning it on makes that camera the start, turning it off
  returns the scene to Automatic, and the automatic camera shows "(automatic)".
  One reference means two cameras never both claim the start.
- The input contexts move here from the Scene References section, which keeps
  its other lists; maintained projects migrate once and no compatibility reader
  remains. A context listed here is a cook dependency of the scene.
- References follow renames, moves and reparenting. A deleted or
  no-longer-camera node, or a missing context, shows as missing and fails the
  cook with Go To Property. Edits are undoable scene commands.

At runtime, scripts switch cameras with `oxygen.scene.set_active_camera(node)`
and contexts with `oxygen.input.activate_context` and `deactivate_context`
([player host](../../../projects/Oxygen.Engine/design/runtime-core/scene-instantiation-and-player.md#scene-start-state)).
Editor panes keep their own navigation cameras and input.

### Play control

The player is an external process whose window lives as long as the session:
Play starts it, Stop and closing its window end it. Restart and playing another
scene reuse the running player, so they start warm. One session runs at a time,
application-wide. The scene document toolbar holds the Play transport, one
capsule with, left to right:

- **Play**, which reads **Restart** while this scene plays. The button is as
  wide as its longer label, so nothing after it moves when the label changes.
  Its icon is green for Play, accent for Restart, and amber, with an amber
  outline, for Restart after the scene changed.
- An arrow that opens the player launch settings. It is a button of its own, so
  the settings stay open to change while Play is unavailable.
- **Stop**, always present. It cancels a Play on its way (its tooltip reads
  Cancel) or closes the player, and is disabled when there is neither.
- After a divider, the **status**: an icon and a short text, always shown.
- While Play is on its way, an indeterminate line runs along the capsule's
  bottom edge.

| Status                 | Icon          | Shown while                                | Click shows   |
| ---------------------- | ------------- | ------------------------------------------ | ------------- |
| Ready · Standalone     | Grey dot      | No player runs                             | Nothing       |
| Saving…                | Progress ring | Play saves the open documents              | Nothing       |
| Cooking scene…         | Progress ring | Play cooks the scene                       | Cooking panel |
| Starting player…       | Progress ring | The player process starts                  | Output panel  |
| Loading scene…         | Progress ring | The player loads the scene                 | Output panel  |
| Playing · 60 FPS       | Green dot     | A session runs; the rate from `stats`      | The player    |
| Paused                 | Grey dot      | The session is paused in the player        | The player    |
| Scene modified         | Amber dot     | A document changed since the session began | The player    |
| Player open            | Grey dot      | The session was ended in the player        | The player    |
| Cook failed · 3 errors | Error badge   | The last Play failed                       | Its panel     |

The green dot breathes while the scene plays. A status that brings the player
forward ends with an open-window icon. The failure badge names the failure
(save, cook, scene, launch, player missing or stopped) and the error count of
the panel it opens: Problems for a scene or script failure, Cooking for a cook
failure, Output for a player that did not start or stopped. The status names the
scene when it is another scene than the toolbar's, for example "Arena · Playing
· 60 FPS". Its tooltip says what it means and what clicking it shows. Pause and
frame step stay on the player's own toolbar, where the user is looking.

- **Shortcuts:** F5 plays or restarts, Shift+F5 stops or cancels. They work from
  any workspace pane, like the other scene shortcuts.
- **Which scene:** the scene of the toolbar, which is the focused scene
  document. In another scene's toolbar, Play's tooltip reads "Stop _Level1_ and
  play this scene".
- **Unavailable:** Play is disabled while a Play is on its way, in a scene
  without any camera ("Add a camera to play this scene"), and without a player
  in the SDK (its tooltip names the path it looked for).

The launch settings flyout is one flat card titled "Player launch settings",
with **Reset** (every setting back to its default) beside the title, and no
dividers or subtitles:

| Row               | Default    | Controls                                                                                                               |
| ----------------- | ---------- | ---------------------------------------------------------------------------------------------------------------------- |
| Display mode      | Windowed   | Windowed or Borderless, as segments. Borderless covers the player's display at the desktop resolution.                 |
| Window size       | 1280 × 720 | 1920 × 1080 (Full HD), 2560 × 1440 (QHD), 3840 × 2160 (4K UHD), 1280 × 720 (HD) or Custom…; disabled while Borderless. |
| Custom size       | Hidden     | Shown for Custom…: width and height boxes in pixels, side by side.                                                     |
| VSync             | On         | The name on the left; On or Off, then the switch, at the right edge, as in Windows Settings.                           |
| Limit frame rate  | Off        | The same switch row.                                                                                                   |
| Frames per second | 60         | Shown while the frame rate is limited: the name on the left, a scrubbable box (1 to 240) on the right.                 |

Sizes are framebuffer sizes in physical pixels, so 3840 × 2160 is 4K at any
display scale. Custom keeps the size typed in even when it has a name, and a
size no entry names reads as Custom. There are no aspect ratios: the camera owns
the aspect ratio.

A running player follows every change at once: the display mode through
`host.window.mode`, the window size through `host.window.size`, VSync through
`gfx.vsync` and the frame rate limit through `ngin.target_fps`. A borderless
window receives its size when it is framed again. A new player starts with the
settings from its launch file, and receives them once more when it connects,
which covers a change made while it started. A value that changes again while
its request is in flight is sent once more with its latest value, so scrubbing a
size sends what the player can follow. A setting the player refuses, such as a
size while its window is minimized, is reported in Output. Play does not send
the size again, so a player window the user resized keeps its size across
Restart.

The settings are per-user editor settings, persisted across restarts
([settings LLD](settings-architecture.md#55-play-session-preferences)). They are
not scene data and are never cooked.

### Starting a session

1. **Save.** Play saves every dirty document without asking; its tooltip reads
   "Save, cook and play". Saving does not touch undo history. A failed save
   stops Play with the save's result.
2. **Cook** the active scene's closure incrementally. A failure stops here with
   its result in the cooking panel.
3. **Write the launch file** to the project's local (ignored) state folder:
   project cooked mounts in order, the scene, the project's authoring roots as
   script source roots, the session settings and a `control` block with
   `exit_when_owner_leaves`. The editor holds the cooked roots it names until
   the player reads others.
4. **Launch or reuse.** No player: start `Oxygen.Player.exe` from the installed
   SDK on the launch file, read the discovery file of that process and connect
   to its
   [control server](../../../projects/Oxygen.Engine/design/runtime-core/commands-and-remote-control.md#4-control-server)
   as owner; the player plays the launch file's scene. A player running: end its
   session, then run `ngin.session.begin` with the scene; the player reads the
   launch file again and remounts, so the cook from step 2 is always seen.
5. **Focus.** On every session start the player window comes to the front with
   input focus, so the game reacts to keys at once.

Stop during steps 1 to 4 cancels Play at the next step boundary; a cook in
progress is cancelled, and a player that was starting ends.

### While a session runs

- The editor stays fully editable; edits are ordinary authored edits and are
  never rolled back.
- Saving a script reloads it in the player without a cook.
- **Stale session:** Play saved every document, so an edit to any open document
  after the session started (the scene, a material) turns the status to "Playing
  · scene changed" and Restart's tooltip to "Restart to play your changes".
  Script edits do not: they reload live.
- Output and Problems are cleared on `session_started`. `log` events go to
  Output, `diagnostic` events to Problems (SD-10), and `scene_failed` issues to
  Problems with their node and an operation result.

### Ending a session

- **Stop**, closing the project or closing the editor runs `ngin.quit` and waits
  briefly before ending the process.
- Closing the player window ends the session; the status clears.
- A Play that fails ends the player, so a failed status never has a player
  behind it. The status shows the failure; clicking it shows the panel with the
  details.
- A connection that closes without `stopping` reports "The player stopped
  unexpectedly" with the last log lines in Output.

### Left out

| Feature                            | Reason                                                                                                                                                   |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Play from here                     | Oxygen has no player object; the camera is driven by scripts or rigs, and moving only the view would fight them. It returns with a player-start concept. |
| One-off starting-camera override   | Changing the field is one undoable edit; a hidden override makes the start disagree with the scene.                                                      |
| Pause and frame step in the editor | The player's toolbar has them where the user is looking; the editor has no live view of the running scene. The editor shows the Paused state.            |
| Playing inside the editor viewport | SD-2: the external player is the shipped runtime and isolates crashes.                                                                                   |
| Project main scene, Play project   | Belongs to packaging, post-V0.1.                                                                                                                         |
| Script debugger                    | The engine has no debugger protocol; logs and diagnostics with spans are the V0.1 debugging tools.                                                       |

## 9. Problems

A dockable Problems panel lists script diagnostics from analysis and from the
running session, grouped by file, with severity, code and message. Selecting an
entry renders its full diagnostic (spans, labels, notes, help) in a monospace
detail view using the engine's layout; the code links to its explanation.
Double-click opens the code editor at the primary span. Runtime entries name the
node and slot and select the node on request. Analysis entries clear when the
file is fixed; session entries clear on the next Play.

Problems docks as a tab beside Output and Cooking. Its title carries the error
and warning counts, and the script's tile and inspector card carry badges. A
save never brings Problems forward, so it never takes focus from the code
editor; runtime errors during Play do (SD-10).

## 10. Operation results

Editor-side codes: `OXE.SCRIPT.*` (create, launch the code editor, analysis
unavailable, identity collision, input file referencing another file's action,
project tooling not updated, slot script missing, slot node missing, slot
without script) and `OXE.PLAY.*` (cook failed, player missing from the SDK,
launch failed, session ended unexpectedly). Engine diagnostic codes (`S0xxx`,
and the cooker's `script.sidecar.*`) stay as reported; sidecar diagnostics name
the node, slot and parameter they are about.

## 11. Exit gates

| ID   | Check                                                                                                                                                                                                                                                                                                                                             |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SA-1 | New Script writes the template and opens it; Edit opens the configured editor at a diagnostic's line.                                                                                                                                                                                                                                             |
| SA-2 | Saving a script updates its parameter fields and Problems without a cook.                                                                                                                                                                                                                                                                         |
| SA-3 | Slot and override commands undo/redo; save/reopen round-trips every value type, order and execution order.                                                                                                                                                                                                                                        |
| SA-4 | Asset and node references survive rename and move; a deleted node shows as missing and fails the cook visibly.                                                                                                                                                                                                                                    |
| SA-5 | Cook emits script and sidecar jobs; RenderScene and the player run the cooked scene with the authored values.                                                                                                                                                                                                                                     |
| SA-6 | Play saves, cooks and launches with the session settings on the starting camera and input contexts and brings the player forward; a running player follows settings changes; it streams logs and a runtime diagnostic; a script edit reloads live; a scene edit marks the session stale; Restart restarts the session and Stop closes the player. |
| SA-7 | Session settings survive an editor restart; a hand-written input file cooks and its actions appear in parameter pickers.                                                                                                                                                                                                                          |
| SA-8 | Owner review in the running editor of SC1–SC8.                                                                                                                                                                                                                                                                                                    |
