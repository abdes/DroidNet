# Model Editor LLD

Status: **design for owner review** (ED-M09 item 14, with
[scene instances](scene-instances.md)).

Last Updated: 2026-10-28

## 1. Purpose

Let the user see what a model import gave them, check it, and tune how it is
imported, without building a scene first. Opening `Car.glb` opens the Model
Editor in the workspace's document area, as a tab beside the scene: the same
toolbar and viewport as the scene editor, set up for looking rather than
editing, and the model's import settings beside the viewport. The workspace
around it does not move: the docked Scene Explorer and Properties panes show
the model's nodes, and the Content Explorer, Output, Cooking, Problems and
Cooked Inspector panes stay where they are. From there the car is one click
away from the open scene.

This replaces the model view of
[#80](https://github.com/abdes/DroidNet/issues/80) scenario R2. Unreal's
Static Mesh Editor and Godot's Advanced Import Settings play the same role:
the model opens as itself, with a preview, its structure and its import
settings in one place.

Mockups (dark theme): [model-editor.svg](mockups/model-editor.svg), and the
import dialog with one file selected,
[model-import-dialog.svg](mockups/model-import-dialog.svg), and with several,
[model-import-dialog-selection.svg](mockups/model-import-dialog-selection.svg);
the Import run flyout,
[model-import-dialog-run.svg](mockups/model-import-dialog-run.svg).

## 2. Scenarios

| ID  | Scenario                                 | What the user sees                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| --- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M1  | Open what was imported                   | Double-clicking `Car.glb` in the Content Explorer adds a `Car.glb` tab beside `Harbor` and activates it: the car framed in the viewport, its nodes in the Scene Explorer, its import settings beside the viewport. No pane moves                                                                                                                                                                                                                                    |
| M2  | Look at a part                           | Clicking a door in the viewport, or `Door_FL` in the Scene Explorer, selects it; the Properties pane shows its transform, mesh, LODs and surfaces with their materials, read-only; F frames it                                                                                                                                                                                                                                                                      |
| M3  | Find what went wrong                     | The toolbar's status reads "Imported · 3 problems"; clicking it shows the Problems pane with the missing `Paint_Normal.png` (rendered with the error texture), a texture found in `textures/` by name and a dropped animation                                                                                                                                                                                                                                       |
| M4  | Fix the import                           | The model is in centimetres: the user sets Units to Custom, scale 0.01 in the import settings, and presses Save; the model is reimported, the view updates, and every scene that places the car shows the corrected size                                                                                                                                                                                                                                            |
| M5  | Set what every car uses                  | With `Body` selected, the import settings show its geometry; the user sets the LOD policy to Distance and turns Cast Shadows off on the Glass surface. Every placed car starts from these defaults                                                                                                                                                                                                                                                                  |
| M6  | Place it                                 | _Add to scene_ on the toolbar places a car in Harbor at the scene viewport's focus point, activates the Harbor tab and selects the new instance root                                                                                                                                                                                                                                                                                                                |
| M7  | Go from a placed car to its model        | In Harbor, _Open model_ on `Car3`'s context menu, or the "in Car (model)" link in the Properties pane, opens or activates the `Car.glb` tab with the same node selected                                                                                                                                                                                                                                                                                             |
| M8  | Reimport after the artist exported again | The status reads "Source changed" and _Reimport_ is enabled; the view, tree and problems update                                                                                                                                                                                                                                                                                                                                                                     |
| M9  | Switch back and forth                    | Clicking the Harbor tab shows Harbor with its selection, camera and history as they were; clicking `Car.glb` shows the car again. Assets loaded for either stay loaded, and no pane moves                                                                                                                                                                                                                                                                           |
| M10 | Undo a setting                           | Every import-setting change is one step in the tab's history until Save                                                                                                                                                                                                                                                                                                                                                                                             |
| M11 | Choose the glTF scene                    | `Car.glb` holds two scenes, Night (the default) and Day. The problems list says the model imports Night and names Day as not imported, with a link to the setting; the user chooses Day in the Scene setting and presses Save; the model is reimported with Day's nodes                                                                                                                                                                                             |
| M12 | Import with the right settings           | _Import > Source model…_ in the Content Explorer opens the Import model dialog, and _Add files…_ adds `Car.fbx`. The dialog reads "FBX 7.4 · centimetres, Z up · 14 nodes · 9 meshes · 6 materials · 11 textures", lists a dropped animation, and shows the import settings; the user points the Glass material at the project's `Glass.omat` and presses Import. The car arrives upright, at the right size, using `Glass.omat`, ready to open in the Model Editor |
| M13 | Import several files                     | Three files are picked and all are selected in the dialog's list, one with a warning glyph. The user sets Normals to Always recalculate once for all three, selects `Car.fbx` alone to point its Glass material at `Glass.omat`, and presses Import 3: the three cook as one run                                                                                                                                                                                    |
| M14 | Import the same files again              | The user saves the session from the dialog as `Vehicles.import-manifest.json` beside the project. After a fresh checkout, _Import > Source model…_ and _Open session…_ open it: the three files are listed with their settings; `Truck.glb`, no longer on disk, shows the error glyph and is removed; Import brings in the two others. Later, the vendor sends new versions: the session opens with both names taken, Ctrl+A and _Replace_, Import                  |

## 3. Decisions

| ID    | Decision                                                                                                                                                                                                                                                                                                    | Reason                                                                                                                                                                                                                                                               |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ME-1  | The Model Editor is a document in the workspace's document area, its tab beside the scene's, and it stays in the workspace window like the scene editor                                                                                                                                                     | The engine shows one scene at a time; a model shown in another window would need a second scene in the same frame                                                                                                                                                    |
| ME-2  | The engine shows the active document: the scene while its tab is active, the model's preview scene while a model tab is active. Switching rebuilds the shown engine scene from its document; loaded assets stay                                                                                             | As RenderScene switches scenes: the native asset loader keeps loaded assets within its budget, so switching is cheap and the engine needs no second scene                                                                                                            |
| ME-3  | Opening or activating a model changes no layout: no pane opens, closes, moves or resizes. The docked Scene Explorer and Properties panes show the active document's nodes and selection                                                                                                                     | The user's workspace is theirs; panes that follow the active document are the panes they already arranged                                                                                                                                                            |
| ME-4  | The model's structure and values are read-only; only import settings are edited                                                                                                                                                                                                                             | The model is an import output (scene instances SI-2): a change for one placement belongs to a scene, a change for every placement to the import settings                                                                                                             |
| ME-5  | The import settings are a panel of the model document, right of the viewport: model-wide groups, then the selected mesh's geometry defaults and the selected texture's settings                                                                                                                             | They are what the document saves (Ctrl+S), as the Texture Editor's settings pane is; the Properties pane stays the inspector of nodes, read-only here as the model is                                                                                                |
| ME-6  | Settings take effect on Save, which writes the import settings and reimports; until then the view shows the last import, as the enabled Save and the tab's unsaved mark say                                                                                                                                 | The Texture Editor's rule: the view never approximates unsaved settings                                                                                                                                                                                              |
| ME-7  | Opening a model opens its Model Editor; _Reimport_ is an explicit command (the toolbar, the Content Explorer's context menu)                                                                                                                                                                                | Double-click reimports today, which shows nothing and redoes work                                                                                                                                                                                                    |
| ME-8  | The toolbar is the scene editor's `ToolBar` with the model's commands in the same places: Save, Reimport, Add to scene, Browse to asset, the status, Settings                                                                                                                                               | Same control, same positions, same shortcuts: Ctrl+S saves, Ctrl+B browses, and the status sits where the Play status does                                                                                                                                           |
| ME-9  | The viewport is the scene editor's `Viewport` in its viewing profile: navigation, view mode, overlays, Frame and the status strips stay; editing tools, snapping, layouts and Maximize are hidden                                                                                                           | Nothing in the model can be moved, so tools that move things only add noise; one model needs one view. The profile is a property of the control, not a copy of it                                                                                                    |
| ME-10 | Import settings are the choices that change what the user gets and differ from one model to the next (section 5.3); what the importer decides from the file (texture formats by use, mipmaps, content hashing) is a rule, not a setting. A model always imports its textures, materials, geometry and scene | A setting the user cannot judge only adds a way to break an import; each setting listed answers a problem real glTF or FBX files have. The scene is required to place the model (scene instances SI-2), and the other products to show it                            |
| ME-11 | The import settings panel is one control, shown beside the Model Editor's viewport and in the import dialog                                                                                                                                                                                                 | The user meets the same rows, defaults and override bars at the first import and at every later fix, and the two cannot drift apart                                                                                                                                  |
| ME-12 | The import dialog analyses each file before importing it and shows its facts, its problems and the settings; it has no 3D preview                                                                                                                                                                           | The analysis reads the file's structure without its buffers, so it is fast enough for a dialog; a preview would cost an import, and the Model Editor is that preview, one click away                                                                                 |
| ME-13 | With several files, the import dialog edits the selected files at once, as the Properties pane edits a multi-selection: shared values show, differing ones read Mixed, an edit applies to every selected file; rows bound to one file's content (scene, materials) need that file selected alone            | Units, axes, normals, pivots, textures and lights are policies valid for any file, set once for a batch; a one-time copy to the other files (an _Apply to all_ command) goes stale at the next edit, and the inspector's mixed values already say which files differ |
| ME-14 | The files of one import dialog cook as one run, and the dialog offers the ImportTool's execution settings (threads, jobs at once, pipeline workers and queues), which an import session saves with its files                                                                                                | The ImportTool runs a batch's jobs concurrently; the execution settings belong to the import, not to a model, so no record keeps them, while a session, which is the import, does                                                                                    |
| ME-15 | An import session is saved as an ImportTool import manifest: one gltf or fbx job per file, with its source, identity, name and settings, and the run's execution settings                                                                                                                                   | One file runs the same import from the editor and from the command line, and the engine's schema already holds every setting; a separate session format would be a second description of the same import                                                             |

## 4. The document

Models are an asset family with an editor, so they get a family module, as
Materials has ([asset family modules](asset-family-modules.md) section 5.1):
`Oxygen.Editor.Models` owns the model's import settings (`ModelImportSettings`,
the editor's side of the model import record; ContentPipeline cooks and
imports but states no family rule), `ModelDocumentKind`, `ModelContent` and the
model's problems; `Oxygen.Editor.ModelEditor` holds the document's view and
view model, as `Oxygen.Editor.MaterialEditor` does for materials.

`ModelDocumentKind` opens a model source file (`.glb`, `.gltf`, `.fbx`); the
tab's title keeps the extension (`Car.glb`). The document's content,
`ModelContent`, holds the model's source and follows it when it moves, and the
model's import settings, `ModelImportSettings`, read from its model import
record (`<ProjectRoot>/imports/<source_identity>.import.json`,
[scene instances](scene-instances.md) NGN14), which Save writes atomically and
its history records. The editor never writes the source file.

The **preview scene** is built in memory when the document is first shown and
never saved: one instance root of the model (a model instance, scene instances
section 3) at the origin, named after the model, and a preview environment: a
physical sky, a directional light that is the sky's sun (100 000 lx), a sky
light captured from the sky, and a fixed manual exposure (EV100 14), so the
background never drives the model's exposure. Models ranks below Scenes, so
`DocumentScenes` in `Oxygen.Editor.Scenes` owns it: it answers which scene any
open document shows, a scene document's own or a model document's preview, and
Live Preview, the Scene Explorer and the Properties pane all ask it. The preview
is kept in a file beside the model, `Car.glb.preview.json` (the model's whole
file name, so `Car.glb` and `Car.fbx` each have their own): its environment and
its sun, in the editor's scene format, without the model it places. The file is
written shortly after each change and when the document closes, and read when
the preview is built; a missing or unreadable file shows the default preview.
It stands for no asset: the Content Explorer does not list it and the cook
ignores it, it moves, renames and is deleted with its model, and a relocation
rewrites the references in it as in a scene. Whether it is shared through
version control is the project's choice. The preview scene's identity is the
model's source identity, so per-user view state, the viewport's camera and view
options, is kept across renames as a scene's is (`ViewportStateService`). The model
in the preview is read-only (`SceneReadOnly`): every edit of the model or of
the scene's structure is refused with `Scene.ReadOnly`, whose reason names the
model. How the model is looked at stays editable: the environment and the sun
take edits, recorded in the preview's own history so the model's document
never shows a change; the viewport's _Preview environment_ and exposure change
the preview outside any history.

Live Preview projects the preview scene like any scene: the instancing, the
picking and the instanced-node selection of
[scene instances](scene-instances.md) do all the work. One Model Editor exists
per model; opening the model again activates its tab, and double-clicking an
imported model in the Content Explorer opens it. Closing the tab asks to save
unsaved settings.

## 5. Layout

The document fills the document area as the scene editor does: the toolbar on
top, then the viewport and, on its right, the import settings panel, separated
by a splitter. The panel's width and whether it is collapsed are per-user view
state, shared by every model document; collapsed, it takes no room, and the
toolbar's _Import settings_ toggle shows it again. Everything else is the workspace's own
panes.

### 5.1 Toolbar

The scene editor's `ToolBar`, with the model's commands where the scene's
equivalents sit:

| Position             | Scene editor                      | Model Editor                                                                                                                                                                                                 |
| -------------------- | --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| First group          | Save (Ctrl+S), Cook scene         | Save (Ctrl+S): writes the import settings and reimports; Reimport, enabled when the source changed or the last import failed                                                                                 |
| Second group         | Add, Browse to asset, Environment | Add to scene, with its label; Browse to asset (Ctrl+B): the selected node's geometry or material, else the model file                                                                                        |
| Transport and status | Play, Stop, "Ready · Standalone"  | The import status when it asks for attention, "Source changed", "Importing…", "Import failed" (a current import says nothing), and the problem count, a link to the Problems pane filtered to the model      |
| End                  | Settings                          | Settings: the same preview settings flyout (frame pacing, grid), shared with the scene editor; _Reset preview_ returns the sky, sun and exposure to the defaults; _Import settings_ shows or hides the panel |

The scene's Add menu, Environment and the Play transport do not apply to a
model and are absent, not disabled.

### 5.2 Viewport

The scene editor's `Viewport` control gains a **viewing profile** (`Editing`,
the scene editor's, and `Viewing`), a control property every overlay element
follows. In `Viewing`:

| Viewport element                                  | Viewing profile                                                                                |
| ------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| View camera (Turntable / Fly, direction, lens)    | Shown                                                                                          |
| View mode (Lit, Unlit, Wireframe, …)              | Shown, with an Exposure slider: stops added to the preview's fixed exposure, −4 to +4          |
| Show (grid, selection outline, icons, statistics) | Shown; the camera preview entry is hidden (a model has no authored cameras to preview through) |
| Preview environment (in Show)                     | Shown: lights the model with the preview's sky and sun; cleared, only the model's own lights   |
| Frame selection (F, Shift+F)                      | Shown                                                                                          |
| Select, Move, Rotate, Scale tool strip            | Hidden; a click selects                                                                        |
| Transform space, snapping and its increments      | Hidden                                                                                         |
| Viewport layout                                   | Hidden: one perspective view                                                                   |
| Maximize                                          | Hidden: the single view already fills the viewport                                             |
| Viewport settings                                 | Shown                                                                                          |
| Gesture hint and statistics strips                | Shown                                                                                          |
| Transform gizmo and dragging                      | None                                                                                           |

A click selects the node under the cursor (no instance-first rule: the model
is the whole document); Show's selection outline marks it as in scenes.

### 5.3 Import settings panel

One control, `ModelImportSettingsPanel`, shown beside the Model Editor's
viewport and in the import dialog (section 7, ME-11). It is a
`PropertiesExpander` stack of the editor's shared property editors (`PropertyCard` with its
override bar and reset, `NumberField`, `ToggleField`,
`Oxygen.Editor.Controls/src/PropertyEditors/`; `AssetPickerButton` of
`Oxygen.Editor.Pickers`), so every row looks and
behaves as in the Properties pane: a set value carries the override bar and
reset, an unset one shows the importer's default in secondary text. A row that
does not apply to the file is absent, not disabled: the up axis for a glTF
file, the scene for a file with one scene. The panel fills from the source
analysis, which reads a file's structure without loading its buffers (M-NGN2),
so it is complete before the first import.

The panel builds nothing of its own; each row takes what the inspector's
controls already give:

- **Groups** are `PropertiesExpander`s with the header icon, title and one-line
  description of the Properties pane's sections.
- **Every row is a `PropertyCard`**: its label column, the override bar (full,
  or half for a mixed selection), the reset button, the qualifier line ("FBX
  files") and its responsive layout, which stacks the label above the editor
  when the panel is narrow.
- **Numbers are `NumberField`s**, with their unit or prefix (smoothing
  angle in °, light range in m, scale and intensity as × factors), their
  validation (a value outside the setting's range is refused with the field's
  error text, the range being the manifest schema's), label scrubbing and the
  indeterminate state for Mixed.
- **Toggles are `ToggleField`s** (Mixed shows as its Mixed text), **choices
  are combo boxes** in a `PropertyCard` (Mixed as the placeholder), and
  **materials use the inspector's material picker**, `AssetPickerButton`
  (extracted from the Geometry section's surface cards, see Ownership in
  section 7): a stretched `SplitButton` in a `PropertyCard`, its content the chosen material's name
  (_Imported_ while unset, `--` for a mixed selection) and its chevron opening
  the `AssetPicker` flyout.

`NumberField` had no override bar or reset, while
`PropertyCard` and `ToggleField` do; it gains them (`OverrideState`,
`ResetCommand`, `ResetToolTip`, passed to its card) as an enhancement of the
control with its own tests, which the Properties pane can then use too. The
Import run flyout of the dialog (section 7) is built from the same rows.

Each setting answers a problem real files have, and the format differences
decide which rows exist:

| Group              | Setting                                                  | Values, default first                                                                                                                                                                                                                                                                                                                                                                                                                                 | Why it is a setting                                                                                                                                                                                                       | Engine                                |
| ------------------ | -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| Source             | Scene                                                    | The file's default scene; any other by name, an unnamed one by index (`#2`)                                                                                                                                                                                                                                                                                                                                                                           | A glTF file can hold several scenes; shown only then (M11)                                                                                                                                                                | NGN12                                 |
| Units and axes     | Result                                                   | Read-only: what the import does to the model's size and orientation in Oxygen's space (right-handed, Z up, metres; `Core/Constants.h`), such as "Real size kept: converted from centimetres to Oxygen's metres (1 file unit = 0.01 m). Turned from Y up to Oxygen's Z up."; the size sentence first says the outcome for the model (real size kept, not converted and how much larger, a chosen multiple), then what one file unit measures in Oxygen | A model a hundred times too large or lying on its back is the most common FBX import problem; saying what the import will do, not what the file states (the facts line has that), explains it before anything is imported | M-NGN2                                |
|                    | Units                                                    | Metres (the file's unit converted); File units (one file unit becomes one metre); Custom                                                                                                                                                                                                                                                                                                                                                              | FBX files from Maya and 3ds Max are in centimetres, from Blender in metres; glTF is in metres by specification                                                                                                            | Exists (`unit_policy`)                |
|                    | Scale                                                    | 1.0; shown with Custom                                                                                                                                                                                                                                                                                                                                                                                                                                | A file authored at the wrong size                                                                                                                                                                                         | Exists (`unit_scale`)                 |
|                    | Up axis                                                  | From the file; Y up; Z up. FBX only                                                                                                                                                                                                                                                                                                                                                                                                                   | Some exporters write no axes or wrong ones (`fbx.axis_unknown`); glTF is +Y up by specification, so the row is absent for it                                                                                              | M-NGN3                                |
| Geometry           | Normals                                                  | From the file, generate missing; Always recalculate                                                                                                                                                                                                                                                                                                                                                                                                   | Bad exported normals are fixed by recalculating; a glTF primitive without normals stays faceted, as its specification requires, unless recalculated                                                                       | Exists (`normals_policy`)             |
|                    | Smoothing angle                                          | 60°; applies to every normal the import generates: missing ones, or all with Always recalculate                                                                                                                                                                                                                                                                                                                                                       | Generated normals without an angle are all smooth or all faceted, both wrong on hard-surface models                                                                                                                       | M-NGN4                                |
|                    | Tangents                                                 | From the file, generate missing (MikkTSpace); Always recalculate                                                                                                                                                                                                                                                                                                                                                                                      | Normal maps baked in MikkTSpace need MikkTSpace tangents, which glTF requires when a file has none                                                                                                                        | Exists (`tangents_policy`)            |
|                    | Keep node pivots                                         | On; Off: node transforms are baked into the meshes, and equal meshes are shared                                                                                                                                                                                                                                                                                                                                                                       | A change rotates a node about its pivot (opening a door, scene instances S6); baking moves every pivot to the model's origin                                                                                              | Exists (`bake_transforms`), M-NGN10   |
|                    | Generate LODs                                            | Off; On, with levels (3, from 1 to 7) and reduction per level (50%)                                                                                                                                                                                                                                                                                                                                                                                   | Most glTF files carry one LOD; LOD groups and `_LODn` meshes of an FBX file are always imported as authored                                                                                                               | M-NGN5                                |
| Hierarchy          | Drop empty nodes                                         | Off; On                                                                                                                                                                                                                                                                                                                                                                                                                                               | Exporters add empty grouping nodes; anchors are taken before pruning, so dropping them keeps node IDs (scene instances NGN2)                                                                                              | Exists (`node_pruning`)               |
| Node continuity    | Continued nodes                                          | See below                                                                                                                                                                                                                                                                                                                                                                                                                                             | A node renamed or moved in the DCC tool keeps its changes (scene instances SI-16)                                                                                                                                         | NGN2                                  |
| Textures           | Maximum size                                             | Source size; 4096; 2048; 1024; 512                                                                                                                                                                                                                                                                                                                                                                                                                    | Downloaded models carry 8K textures that cost memory and cook time                                                                                                                                                        | M-NGN7                                |
|                    | Compression quality                                      | Default; Fast; High                                                                                                                                                                                                                                                                                                                                                                                                                                   | BC7 encoding trades cook time for quality                                                                                                                                                                                 | Exists (`bc7_quality`)                |
|                    | Flip normal map green                                    | Off; On                                                                                                                                                                                                                                                                                                                                                                                                                                               | Normal maps made with the DirectX convention, common in FBX pipelines, light backwards otherwise                                                                                                                          | Exists (`flip_normal_green`)          |
| Lights and cameras | Import lights                                            | On; Off                                                                                                                                                                                                                                                                                                                                                                                                                                               | Props often carry the artist's preview lights                                                                                                                                                                             | M-NGN8                                |
|                    | Light intensity scale                                    | 1.0                                                                                                                                                                                                                                                                                                                                                                                                                                                   | FBX intensities have no unit and are converted by a rule (`FbxIntensityToLumens`); one factor corrects a file that comes out too bright or too dark                                                                       | M-NGN8                                |
|                    | Range of lights without one                              | 4096 m; glTF only                                                                                                                                                                                                                                                                                                                                                                                                                                     | A glTF light without a range reaches infinitely far                                                                                                                                                                       | Exists (`gltf_omitted_light_range_m`) |
|                    | Import cameras                                           | On; Off                                                                                                                                                                                                                                                                                                                                                                                                                                               | A model's cameras are often turntable or render cameras                                                                                                                                                                   | M-NGN8                                |
| Materials          | One row per source material                              | Imported; a project material, chosen with the asset picker                                                                                                                                                                                                                                                                                                                                                                                            | A project usually already has the materials an imported model should use; the replaced material is not produced                                                                                                           | M-NGN6                                |
|                    | _Match by name_ (header action)                          | A command in the group's header: fills each row still Imported whose name matches exactly one project material; its tooltip says so                                                                                                                                                                                                                                                                                                                   | Matching is explicit: a name match is applied only when the user asks, and each filled row can be reset                                                                                                                   | Editor                                |
| Selected mesh      | LOD policy; per surface Visibility and Cast Shadows      | Shown while a mesh node is selected (Model Editor only)                                                                                                                                                                                                                                                                                                                                                                                               | The asset defaults every placement starts from ([content pipeline](content-pipeline.md) 20.5)                                                                                                                             | M-NGN1                                |
| Selected texture   | Maximum size, Compression quality, Flip normal map green | Shown while a texture is chosen from a surface's material (Model Editor only); an override of the Textures group                                                                                                                                                                                                                                                                                                                                      | One texture of a model can need its own size or convention                                                                                                                                                                | M-NGN7                                |

Each setting's engine default equals the panel's, so the ImportTool and the
editor import a file the same way. A setting that does not apply to the file's
format (Up axis for glTF, the light range for FBX) is reported as a warning and
ignored. Rows keyed to parts of the file use the names the source analysis
reports: a mesh's anchor (its storage name, `source_geometry_anchor`), and a
material's or texture's unique name. A row whose key the file no longer has is
kept, inert, and reported (`import.setting_target_missing`, a warning), as a
kept change is.

The importer decides the rest from the file, as rules:

- **Texture formats and mipmaps** follow each texture's use in its material,
  through the engine's per-usage presets (`PresetForUsage`), with a full mip
  chain.
- **Materials** take their alpha mode, cutoff and sidedness from the file. FBX
  materials are converted to metallic-roughness through ufbx's unified PBR
  values, which cover Phong, Lambert, Stingray PBS, Arnold Standard Surface and
  3ds Max Physical.
- **FBX geometric transforms** (pivots) become helper nodes, and space
  conversion is baked into the geometry, so a model's root stays at identity.
- **A missing texture** is reported with its material, which renders with the
  error texture.
- **Compressed glTF data** (meshopt geometry, WebP and KTX2 textures) is
  decoded (M-NGN9).

Left out on purpose:

| Setting                                                           | Why it is not one                                                                                                             |
| ----------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Content hashing                                                   | A build mechanic, forced on in Release builds                                                                                 |
| Texture format, packing, force RGBA, flip Y, mip filter and count | Decided by the texture's use; the Texture Editor sets them on a standalone texture                                            |
| Normals or tangents set to None                                   | Every lit surface needs normals, and normal maps need tangents                                                                |
| Weld vertices, mesh optimisation, index format                    | The cooker's job, the same for every model                                                                                    |
| Lightmap UVs                                                      | Oxygen bakes no lightmaps                                                                                                     |
| Collision generation                                              | Collision shapes come from physics sidecars                                                                                   |
| Combine meshes                                                    | The hierarchy is what scenes instance and change                                                                              |
| Animation, skins, morph targets                                   | Not supported: the import reports them as dropped                                                                             |
| Import visibility, sort by name, root name                        | Not in Oxygen's importer, and not added: they shape other engines' prefab objects, which a model scene has no counterpart for |

The Node continuity group lists what the last import reported: the anchors that
vanished, each saying whether a node continues it, and the anchors that are
new. Each vanished anchor not yet continued offers _Continue…_, a flyout that
lists the nodes new in the last import first, then every node, with Find. A
node that already continues another is disabled; choosing a node that existed
before the last import adds the caution that its changes under its own ID
become kept changes. A vanished anchor no node should continue, a node the
artist deleted on purpose, offers _Forget_: the record no longer lists it, so
later imports do not either (an import carries forward only the vanished
anchors the record holds); changes scenes made on it stay their kept changes.
Below, every continuity entry, from this and earlier imports, has a remove
button. Entries and forgotten anchors are import settings:
they apply on Save, which reimports, and an unsaved entry carries the override
bar. Clicking an anchor selects its node
in the Scene Explorer and the viewport. _Reassign_ in a scene's caution box
opens the same flyout ([scene instances](scene-instances.md) 6.4). Mockup:
[model-editor-node-continuity.svg](mockups/model-editor-node-continuity.svg).

### 5.4 The docked panes on a model

- **Scene Explorer:** the preview scene's row, `Car.glb preview`, holding the
  model's root, `Car.glb`, and its nodes; the sun that lights the preview is
  not listed. The model's rows are the read-only instanced rows of scene
  instances section 6.2, with the Meshes, Lights and Cameras filters; the add,
  folder, rename, cut, copy, paste and delete buttons at its foot are disabled
  with a tooltip naming the model.
- **Properties:** three views, as the selection is:
  - the preview scene's row, or nothing selected: the preview's scene
    settings, how the model is looked at: the sun (azimuth, elevation,
    intensity), the sky, fog, exposure and post-processing. The Play and
    References scopes, which a model has none of, are hidden.
  - the model's root: one card titled with the model's name and its facts
    (source file, last import, and the counts of nodes, meshes, triangles,
    LODs, materials and textures), without the node header, its commands,
    the notice or the root's component sections, which are the preview's.
    Triangles are LOD 0's, summed over the model's geometries, from the
    cooked-geometry report.
  - one of the model's nodes: the Scene Inspector, read-only, with "in
    Car.glb (model)" in its header and a _Read-only_ notice naming the model.
    Its Geometry section shows each LOD's triangle count, in a scene as in
    the model, so the reduction of generated LODs is visible.
- **Problems:** the model's last import diagnostics, kept with its publication
  so they show whenever the document opens; each names its node, mesh,
  material or texture, and selecting it selects that node.

## 6. Switching the shown document

The workspace window has one scene document and any number of model
documents. Live Preview shows the **active** one of them (ME-2):

1. The user activates the `Car.glb` tab. Live Preview retires Harbor's engine
   scene and builds the car's preview scene from its document, as it rebuilds
   a scene document today when another opens ([live preview](live-preview.md)
   decision 1). The native asset loader keeps Harbor's geometries and
   materials loaded within its budget.
2. The user activates the Harbor tab. Live Preview rebuilds Harbor from its
   document snapshot; the assets still loaded are reused, and the car's are
   kept too, so placing a car right after is immediate.

Each document keeps its own selection, editor camera and history across
switches; the Scene Explorer and the Properties pane restore them when their
document becomes active.

## 7. The import dialog

Importing a model opens the **Import model** dialog: the same import settings
panel as the Model Editor, filled from the source analysis, so the first import
is already right instead of fixed by a second one.

**Today.** _Import > Source model…_ in the Content Explorer's Assets pane
(`AssetsView.xaml:92-107`) picks a file and opens `SceneImportDialog`
(`Oxygen.Editor.ContentExplorer/src/Importing/SceneImportDialogViewModel.cs:27`),
which asks only the name, the content folder and whether to replace an
existing model. `ModelImportSettings.Create` then writes the model import
record with the default settings (`ModelImportSettings.DefaultSettings`); no
surface offers a setting, so none can be chosen at import or after.

**The dialog.** It replaces `SceneImportDialog`, and the Content Explorer opens
no file picker for a model any more: _Import > Source model…_ opens the dialog
directly, and files and sessions are added from inside it. With nothing added
yet, one empty state fills the dialog's content instead of an empty list and
an empty settings panel: the title _Add models to
import_, a line saying files can be added, dropped from File Explorer or
brought back from a session, and the buttons _Add files…_ (accent) and _Open
session…_; files dropped anywhere on the content are added, and _Save session…_
and Import are disabled until a file is listed. With files listed and none
selected, the settings side says to select files to see their settings. The
dialog's destination folder is the Content Explorer's selected folder, as
today. Invoking a model source that has never been imported
(`AssetsViewModel.cs:371-381`) opens the dialog with that file. The combined
_Import…_ command keeps its picker, because textures are imported through it;
model files picked there open this dialog with them, images go to the texture
import as today. Mockups: no file yet,
[model-import-dialog-empty.svg](mockups/model-import-dialog-empty.svg); one file selected,
[model-import-dialog.svg](mockups/model-import-dialog.svg); several,
[model-import-dialog-selection.svg](mockups/model-import-dialog-selection.svg);
the Import run flyout,
[model-import-dialog-run.svg](mockups/model-import-dialog-run.svg).

- **Files.** A list on the left, every file selected when the dialog opens.
  Each row shows the file's name and its folder, with no file icon (every row
  would carry the same one), a warning glyph when its analysis found
  a problem, and an error glyph when its name is taken or its file is missing.
  The list is a WinUI list view with extended selection and its standard
  gestures (click, Ctrl+click, Shift+click, Ctrl+A), and the rest of the dialog shows the selection (ME-13). Under it, the
  editor's compact `ToolBar`, icon-only with tooltips that name each shortcut
  (as on the Content Explorer's priority list): _Add files…_ (Ctrl+O; files
  dropped from File Explorer are added too), _Remove_ (Delete), then _Move up_
  and _Move down_ (Alt+Up, Alt+Down). Commands on the selection are disabled
  without one; Move up is disabled while the selection holds the first row,
  Move down while it holds the last. A file that blocks the import is removed instead of cancelling
  the dialog. Rows reorder by dragging, and the buttons do the same from the
  keyboard; the list's order is the order of the run's jobs and of a saved
  session. A row's context menu adds _Reveal in File Explorer_ and, for a
  missing file, _Locate…_, which points the row at the file's new place. Select
  all is Ctrl+A, and there is no Clear: Ctrl+A and Delete do it. With one file
  left the list stays, so more can be added.
- **Layout.** Two columns separated by a splitter: the file list, which starts
  at 220 px, and the import settings panel, which takes the rest (at least
  560 px). The rows above the panel (name, folder, facts) span its width.
  _Before import_ is not a column: it is a flyout of the facts bar's action
  button, so the settings keep the width and the report shows on demand. The
  surfaces are WinUI's own: the `ContentDialog` layers (its content on the top
  overlay, its buttons on the base), the panel's groups as `PropertiesExpander`
  cards, the facts as an `InfoBar`, the report's findings as the editor's
  `DiagnosticCard`s, and the Import run as a flyout; nothing else sets its own
  background.
- **Name and folder.** Name is shown with one file selected. Content folder
  edits every selected file; folders that differ read _Mixed_, and Browse sets
  them all. Whatever blocks a selected file's import is said in words in an
  error `InfoBar` under the folder row, one line per file (a missing source, a
  taken name, a file twice in the import); the list's error glyph only marks
  the row. When a name is taken, the bar's action, _Replace_, approves
  replacing the existing models of the selected files together; a note then
  lists the model each one replaces.
- **Imported again.** A model already imported in place, its source inside the
  project, is not refused: its settings start from its import record, an
  informational bar says Import imports it again with them, and Import writes
  them into its record and reimports it.
- **Facts.** One full-width `InfoBar`. With one file selected, its message is
  one line from the analysis: the format and its version, the detected unit
  and axes, and the counts of scenes, nodes, meshes, materials and textures
  ("FBX 7.4 · centimetres, Z up, right-handed · 14 nodes · 9 meshes · 6
  materials · 11 textures"); with several, how many are selected ("3 files
  selected"). The bar's severity, so its icon, is the most serious finding's
  (error, warning, else informational), and its title counts them ("1 error, 2
  warnings"; none without findings).
- **Settings.** `ModelImportSettingsPanel` without its Node continuity and
  selection groups, which need a previous import, editing the selected files as
  the Properties pane edits a multi-selection: a value the files share shows as
  it is, values that differ read _Mixed_ (an indeterminate number, the toggle's
  Mixed text, a combo box's Mixed placeholder), and the override bar is full
  when every selected file sets the value, half when only some do. An edit or a
  reset applies to every selected file. A row shows when it applies to at least
  one selected file and edits only those; when that is not all of them, its
  qualifier says which ("FBX files"). The rows bound to one file's content, the
  Scene and the Materials group, show with one file selected; with several, the
  panel says to select one file to choose its scene and materials.
- **Before import.** The facts bar's action button, _Before import_, shown
  only when the selected files have findings; the bar's title already counts
  them, so the button has no badge. It opens the report as a light-dismiss
  flyout, which Esc or a click outside closes; the report scrolls inside the
  flyout. The report is what the analysis found: features that will be dropped
  (an animation, a skin), required extensions that cannot be read, textures
  not found. Each is a diagnostic the import itself will report, shown once
  per file: the engine reports a finding for every object it is about (each
  material whose texture sampling is left out), and the item names those
  objects as the file does, from the diagnostic's `object_name` ("Bun, Cheese,
  Sauce and 2 more"; an unnamed one by kind and index, "material #3"). A
  chevron expands the item to list every name in place, one per line; no name
  is only in a tooltip. The item's background is its severity's, so it has no
  icon. Each message says what is left out and what the content is instead.
  With several files selected, the items are grouped under each file's name;
  a file without findings is left out.
- **Import run.** A chevron beside Import opens a flyout with the ImportTool's
  execution settings for this run, so the run's settings belong to the action
  they configure. The chevron
  stays enabled while Import is disabled (a name still taken), and a dot on it
  marks a run with custom settings. The flyout follows the Fluent type ramp and
  the Settings layering: a Subtitle title with a Caption description and
  _Reset all_, then two sections, each a BodyStrong header above a card on the
  flyout's acrylic. **Scheduling** holds Threads (`thread_pool_size`, the CPU
  work pool) and Jobs at once (`max_in_flight_jobs`); Threads and Jobs at once are `NumberField`s.
  **Pipelines** (`concurrency`) is a grid: each pipeline (textures, buffers,
  materials, mesh build, geometry, scene) is a row header, and its workers and
  queue capacity are two in-place `NumberBox`es without labels under the
  Caption column headers Workers and Queue, edited by click or scrubbed by
  drag. There are no separators. An
  unset field shows the ImportTool's default in secondary text, read from the
  installed manifest schema (E11), and its tooltip says what the default is;
  a set value is primary, with the override bar and a reset. They are
  settings of the import, not of a model: they apply to every file of the run,
  no record keeps them, and an import session saves them with its files and
  restores them when it is opened (ME-14, ME-15). A dialog opened without a
  session starts from the defaults.
- **Import session.** _Open session…_ and _Save session…_ on the left of the
  button bar; see below.

The new files of one dialog import as **one cook run**: each is retained and
gets its record, then they cook as one native batch, so the ImportTool runs
them as concurrent jobs under the run's settings. A file that replaces an
existing model runs on its own, after the batch, as its capture and commit
belong to that model alone; it takes the same run settings. The replaced
model's record keeps its source identity, continuity and published nodes, and
the panel starts from its settings; Import saves the chosen settings into it
before the replacement cooks, as the Model Editor's Save does before its
reimport. The dialog closes when the import starts, and progress shows in the
Cooking pane as today. Nothing opens by itself: the Model Editor shows one
model at a time, and the imported models are opened from the Content Explorer.

**Import sessions.** A dialog's files, their identities and settings, and the
run's settings save as an **import manifest**, the ImportTool's own format
(`oxygen.import-manifest.schema.json`), so one file serves the editor and the
command line (`ImportTool batch --manifest … --cooked-root …`). The manifest
already holds everything a session needs; the editor adds no format:

- one `gltf` or `fbx` job per file, in the list's order, with its `source`
  (relative to the manifest's folder when it can be, absolute otherwise), its
  `virtual_path` (the model's identity, which gives the folder and the name),
  its `name`, and each setting the file sets, in the job's own keys;
- `thread_pool_size`, `max_in_flight_jobs` and `concurrency` at the top, when
  the run sets them;
- no `output`: the editor cooks into the project, and the command line names
  its cooked root.

_Save session…_ writes it wherever the user chooses, the project folder
suggested; kept with the project, it lets a fresh checkout import the
originals that live outside the repository again. _Open session…_ adds its jobs
to the list and takes its run settings. The editor validates the manifest against the installed
schema when it reads and writes it. A job whose file is missing stays in the
list with the error glyph until it is removed; a job of another type is left
out with a warning (optional inputs warn and skip). A model of the session that
is already imported reads as a taken name, so reimporting a session is: open
it, select every file (Ctrl+A), _Replace_, Import; each replaced model keeps its
record and takes the session's settings.

Analysis runs when a file is added to the dialog, through the
`analyze-sources` verb the editor already runs for every model
(`Oxygen.Editor.ContentPipeline/src/ImportToolContentPipelineApi.SourceAnalysis.cs:104`):
it parses a glTF file's JSON without loading its buffers, and an FBX file
without building geometry or decoding images (M-NGN2), so the dialog fills
almost at once. The
dialog has no 3D preview: the Model Editor is the preview, one click away
after the import.

**Aura.** WinUI's `ContentDialog` has plain buttons only in its button bar, so
Aura's dialog service gains general options of `DialogSpec`: commands on the
left of the button bar (`FooterCommands`, the session commands), and options of
the primary button (`PrimaryButtonOptions`), opened in a flyout by a chevron
beside it, a button of its own that stays enabled when the primary button is
not, and marked by a dot while the options are modified
(`HasModifiedPrimaryButtonOptions`); the primary button's enabled state is a
predicate of the view model (`IsPrimaryButtonEnabled`). They are Aura features
with their own tests, used here first.

**Ownership.** The panel and the dialog live in `Oxygen.Editor.ModelEditor`,
the model family's UI project. It references the editor's control libraries
(`Oxygen.Editor.Controls`, `Oxygen.Editor.Pickers`), Aura, `Oxygen.Editor.Models`
and `Oxygen.Editor.ContentPipeline`, and never `Oxygen.Editor.SceneInspector`:
what the panel shares with the inspector is extracted into the control
libraries, and the Scene Inspector uses it from there. The material picker,
today XAML of the Geometry section (a `SplitButton` whose flyout holds an
`AssetPicker`), becomes `AssetPickerButton` in `Oxygen.Editor.Pickers`, beside
`AssetPicker` (the Controls library does not reference the asset picker); the
property editors are the library's generic, templated controls, their look
their own default styles in its `Themes/Generic.xaml`, and the keyed styles a
consumer's own elements use (`PropertyNumberBoxStyle`, `PropertyVectorBoxStyle`,
`QuietActionButtonStyle`, `UnitSuffixTextStyle`) are in its
`Themes/EditorStyles.xaml`, which the app merges; the Scene Inspector's styles
no longer set the look of the library's controls. The Content Explorer opens the dialog through
`IModelImportPrompt`, a port of `Oxygen.Editor.Models`, and never references
the Model Editor: a UI surface calls family modules only
([asset family modules](asset-family-modules.md) section 9). The prompt
returns the chosen settings, which `ContentPipelineService.ImportSourceAsync`
writes into the model's import record instead of the default settings.

## 8. Capabilities

The preview is a scene with one model instance, and the panel shows node
continuity and the glTF scene, so the Model Editor needs these engine
capabilities of [scene instances](scene-instances.md) section 7: node
continuity (NGN2), composition and instancing (NGN6 to NGN8), the scene report
(NGN11) and glTF scene selection (NGN12). The import settings need M-NGN1 to M-NGN11. They change the importer,
so they ship in the release of scene instances section 7.15 and share its one
reimport of every model.

| ID      | Capability                   | Today                                                                                                                                                                                                                  | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| ------- | ---------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M-NGN1  | Per-geometry import defaults | The geometry asset already carries them (`GeometryAssetDesc::default_lod_policy`, `SurfaceDesc.flags`, `Data/PakFormat_geometry.h:87-133`), but the model import writes fixed values (`MeshBuildPipeline.cpp:363-374`) | The import settings gain, per geometry anchor, the LOD policy and, per SurfaceId, Visibility and Cast Shadows; the model import writes them into those fields. No geometry format change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| M-NGN2  | Source facts in the analysis | `analyze-sources` reports a model job's files and planned outputs; only `inspect-source` reports units and axes, and neither names materials or textures                                                               | A model job of `analyze-sources` reports the detected unit, axes and handedness, the counts of nodes, meshes, materials and textures, the scenes (NGN12) and the names of its materials and textures, all from the metadata parse (`oxygen.source-analysis.schema.json`); it also reports the file's format and its version (glTF `asset.version`, the FBX file version)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| M-NGN3  | FBX up axis                  | The FBX adapter probes the file's axes and mirrors Y when they are unknown (`FbxAdapter.cpp:1353-1385`); nothing overrides it                                                                                          | `up_axis` (`from-file`, `y`, `z`) in the import manifest job and on the ImportTool; with `y` or `z` the adapter gives ufbx those source axes instead of the probe's                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| M-NGN4  | Smoothing angle              | Normals are generated by ufbx's `generate_missing_normals`, with no angle; no smoothing control for glTF                                                                                                               | `normals_smoothing_angle_deg`, used wherever normals are generated or recalculated, in both adapters                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| M-NGN5  | LOD generation               | A glTF geometry has one LOD (`GltfAdapter.cpp:1909-1911`); nothing simplifies meshes, and every geometry is written with a Fixed LOD 0 policy (`MeshBuildPipeline.cpp:363-374`)                                        | `generate_lods { levels, reduction }`: each geometry with one LOD is simplified with meshoptimizer into further LODs, cooked as authored LODs. Each submesh is simplified on its own, aware of normals and UVs, with its borders locked so seams do not crack; every generated LOD declares LOD 0's SurfaceIds, so surface defaults and scene overrides apply to all LODs. Simplification keeps a subset of the original vertices, so skinned meshes keep their joints and weights and morph targets keep their deltas through the same vertex remap, as Godot and Unreal keep morph targets on generated LODs; the cost, coarser deformation of collapsed areas, shows only at LOD distances. A level that cannot reach its reduction ends the chain with an info diagnostic. A geometry with generated LODs and no LOD policy setting gets a Screen-space error default whose thresholds come from the error meshoptimizer reports per level; a policy set in the import settings (M-NGN1) wins. No geometry format change |
| M-NGN6  | Material remap               | Every source material produces a material                                                                                                                                                                              | `material_remap`, from source material name to material virtual path: its surfaces reference that material, which the import does not produce; a target that does not exist is a warning, and the source material is produced instead                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| M-NGN7  | Texture size and overrides   | No maximum size; `texture_overrides` parses, but no adapter reads it                                                                                                                                                   | `max_size` in texture settings, applied before compression; per-texture overrides applied by texture name in both adapters                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| M-NGN8  | Lights and cameras           | Imported whenever the scene is (`ImportContentFlags`); FBX intensity converted by a fixed rule; FBX film back ignored                                                                                                  | `import_lights`, `import_cameras` and `light_intensity_scale`; an FBX camera takes its vertical field of view from film height and focal length when the file states them                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| M-NGN9  | Compressed glTF data         | meshopt, WebP and KTX2 are unread extensions: the geometry or texture they hold is missing                                                                                                                             | The glTF adapter decodes `EXT_meshopt_compression` (meshoptimizer, with M-NGN5), `EXT_texture_webp` (libwebp) and `KHR_texture_basisu` (KTX2 transcoded to RGBA8, then the ordinary texture path: maximum size, overrides, mips, BC7).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| M-NGN10 | Pivots kept by default       | `bake_transforms` defaults to true in `ImportOptions`; the editor already writes false                                                                                                                                 | The engine default becomes false, so an ImportTool import keeps pivots as an editor import does                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| M-NGN11 | Vertex order                 | Nothing orders triangles for the vertex cache or vertices for fetch; `MeshBuildPipeline` only splits mesh views spatially (Morton order)                                                                               | Within each mesh view of every LOD, triangles are reordered with meshoptimizer for the vertex cache and vertices for fetch; the cooked bytes change, the format does not                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| M-ED1   | Model document               | Double-click reimports a model ([AssetsViewModel.cs:369](../../../projects/Oxygen.Editor.ContentExplorer/src/Panes/Assets/AssetsViewModel.cs))                                                                         | `Oxygen.Editor.Models` (family module: import settings, `ModelDocumentKind`, `ModelContent`, problems) and `Oxygen.Editor.ModelEditor` (UI); opening a model opens it; _Reimport_ becomes a command (ME-7)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| M-ED2   | Shown document               | Live Preview shows the scene document opened last                                                                                                                                                                      | Live Preview shows the active scene or model document and rebuilds on activation (section 6)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| M-ED3   | Panes follow the document    | The Scene Explorer and the Properties pane follow the scene document                                                                                                                                                   | They follow the active scene or model document, with the read-only model presentation of section 5.4; no layout change (ME-3)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| M-ED4   | Every import setting         | The model import record holds the import settings, written with defaults (`ModelImportSettings.DefaultSettings`); no surface chooses them                                                                              | `ModelImportSettings` reads and writes the model import record (scene instances NGN14), with the settings of section 5.3, the node continuity and the published anchors. The product reads no older format: the few existing sidecars become records by hand outside the product, keeping their source identity (scene instances section 7.15)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| M-ED5   | Kept import problems         | Import diagnostics reach the operation results and the Problems pane once                                                                                                                                              | The last import's diagnostics are retained with the model's publication and read when the document opens                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| M-ED6   | Viewport viewing profile     | The `Viewport` control shows every editing overlay                                                                                                                                                                     | `Viewport` gains its `Editing` and `Viewing` profiles (section 5.2), with their own tests; the scene editor keeps `Editing`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| M-ED7   | Tab stays in its window      | Documents other than the scene can be torn out                                                                                                                                                                         | `DocumentEditorDescriptor` (`Oxygen.Editor.Shell/src/Documents/DocumentEditorDescriptor.cs`) gains `CanTearOut`; the model document's provider sets it false                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| M-ED8   | Import dialog                | `SceneImportDialog` asks the name, the folder and the replace choice                                                                                                                                                   | The Import model dialog of section 7 in `Oxygen.Editor.ModelEditor`, opened through the `IModelImportPrompt` port: the selection editing of ME-13, the chosen settings written into each new model import record or saved into a replaced model's record, the new files cooked as one run with its execution settings (ME-14); a file list that adds, removes and reorders files; import sessions saved and opened as import manifests (ME-15)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| M-ED9   | Import settings panel        | None                                                                                                                                                                                                                   | `ModelImportSettingsPanel` (section 5.3), hosted by the model document and the import dialog, filled from the source analysis                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

### 8.1 Generated LODs: stored, derived, discarded

Generated LODs change no cooked format. They are stored in fields the geometry
asset already has; every statistic about them is derived when the descriptors
are read; what the simplifier knows beyond that is used during the cook and
dropped.

| Kind      | What                                                                                                                                                                                                                                    | Where                                                                                                                                                                                                  |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Stored    | Each generated level: its vertex and index buffers, its submeshes with LOD 0's SurfaceIds and default materials, its mesh views, and bounds recomputed per mesh, submesh and view; for skinned and morph meshes, their remapped buffers | One `MeshDesc` per level after the authored LOD; `lod_count` is at most `kMaxLodCount` (8)                                                                                                             |
| Stored    | The Screen-space error policy of a geometry whose policy the settings leave unset                                                                                                                                                       | `GeometryAssetDesc::default_lod_policy`                                                                                                                                                                |
| Derived   | Triangles per LOD and submesh, from `SubMesh::TriangleCount()` (Data): the sum of `MeshView::IndexCount() / 3` over the views the descriptor-only load already reads, with no buffer access                                             | The cooked-geometry report's bindings (`triangle_count`), written by the inspector from that accessor; in the editor, one report helper sums a LOD's bindings for the facts card and the Geometry rows |
| Discarded | Each level's simplification error, the targets and the lock flags                                                                                                                                                                       | Consumed into the policy thresholds during the cook                                                                                                                                                    |

No work exists for statistics. Each count has one source, the Data
accessor above, which every reader calls rather than recounting; nothing
reads vertex or index buffers to count, and nothing records a reduction
ratio, which anyone can read from two triangle counts. The cook computes no
number for reporting: the per-level error is the value meshoptimizer's
simplifier already returns, in the geometry's units, and a diagnostic names
only what the simplification loop already holds.

The runtime's screen-space error is the projected bounding-sphere radius in
pixels, `sse = f · r / z` (`Vortex/ScenePrep/Extractors.h`), not a geometric
error. Level `n`'s simplification error `e_n`, in the geometry's units
(the error meshoptimizer returns with `meshopt_SimplifyErrorAbsolute`, so
no scale is computed), projects to
`sse · e_n / r` pixels. Boundary `i`, between LOD `i` and LOD `i + 1`, takes:

- `enter_finer_sse[i] = r / e_(i+1)`: the finer LOD returns when the coarser
  one's error would exceed one pixel;
- `exit_coarser_sse[i] = 0.8 · enter_finer_sse[i]`: the hysteresis band.

`r` is the radius of the geometry's bounding sphere. Errors grow with the
level, so the thresholds are non-increasing, as the policy requires, and a
uniform scale changes `r` and `e` alike, so they hold for every placed scale.
An error below `10⁻⁶ · r` counts as `10⁻⁶ · r`, so every threshold is finite.
Changing the one-pixel target means a policy set in the import settings or in
a scene, not a new stored value.

The import reports, with no statistics or timings:

| Code                            | Severity | When                                                                                                   |
| ------------------------------- | -------- | ------------------------------------------------------------------------------------------------------ |
| `import.lod_generation.stopped` | Info     | A level could not reach its reduction; names the geometry, the levels made and the requested count     |
| `import.lod_generation.skipped` | Info     | Generate LODs is on and the geometry has authored LODs, which are kept as they are; names the geometry |

## 9. Exit gates

| Gate | Check                                                                                                                                                                                                                                                                                                    |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MG1  | Double-clicking `Car.glb` opens its tab with the car framed and its nodes in the Scene Explorer; nothing is reimported, and no pane moves or resizes                                                                                                                                                     |
| MG2  | Switching between Harbor and `Car.glb` keeps each document's selection, camera and history, and reloads no geometry that is still in the loader's budget                                                                                                                                                 |
| MG3  | Setting a custom scale and saving reimports the car; Harbor's five cars show the new size                                                                                                                                                                                                                |
| MG4  | A Distance LOD policy and a surface's Cast Shadows set on `Body` become the asset defaults every placed car uses, unless a scene changes them                                                                                                                                                            |
| MG5  | A model with a missing texture lists the problem with its material and node on every opening of the document                                                                                                                                                                                             |
| MG6  | _Add to scene_ places the car in Harbor, activates Harbor and selects the new instance root                                                                                                                                                                                                              |
| MG7  | The viewport shows no editing tool, snapping, layout or Maximize control, and dragging in it never moves a node                                                                                                                                                                                          |
| MG8  | Importing an FBX file in centimetres and Z up shows "centimetres, Z up" in the dialog before the import, and the model arrives at its size and upright                                                                                                                                                   |
| MG9  | A source material pointed at a project material in the dialog is used by each of its surfaces, and no material is produced for it                                                                                                                                                                        |
| MG10 | A glTF file whose geometry uses meshopt compression imports its geometry                                                                                                                                                                                                                                 |
| MG11 | With Generate LODs on, `Body` cooks the requested levels with LOD 0's surfaces, and its Geometry section shows each level's triangles reduced by the setting; the facts card shows the model's LOD 0 triangles; a placed car changes LOD with distance, and Cast Shadows off on Glass holds at every LOD |
| MG12 | A glTF file with WebP and KTX2 textures imports them                                                                                                                                                                                                                                                     |
