# Model Editor LLD

Status: **design for owner review** (ED-M09 item 14, with
[scene instances](scene-instances.md)).

Last Updated: 2026-10-25

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
import dialog [model-import-dialog.svg](mockups/model-import-dialog.svg).

## 2. Scenarios

| ID  | Scenario                                 | What the user sees                                                                                                                                                                                                                                                                                                                                                                                                            |
| --- | ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M1  | Open what was imported                   | Double-clicking `Car.glb` in the Content Explorer adds a `Car.glb` tab beside `Harbor` and activates it: the car framed in the viewport, its nodes in the Scene Explorer, its import settings beside the viewport. No pane moves                                                                                                                                                                                              |
| M2  | Look at a part                           | Clicking a door in the viewport, or `Door_FL` in the Scene Explorer, selects it; the Properties pane shows its transform, mesh, LODs and surfaces with their materials, read-only; F frames it                                                                                                                                                                                                                                |
| M3  | Find what went wrong                     | The toolbar's status reads "Imported · 3 problems"; clicking it shows the Problems pane with the missing `Paint_Normal.png` (rendered with the error texture), a texture found in `textures/` by name and a dropped animation                                                                                                                                                                                                 |
| M4  | Fix the import                           | The model is in centimetres: the user sets Units to Custom, scale 0.01 in the import settings, and presses Save; the model is reimported, the view updates, and every scene that places the car shows the corrected size                                                                                                                                                                                                      |
| M5  | Set what every car uses                  | With `Body` selected, the import settings show its geometry; the user sets the LOD policy to Distance and turns Cast Shadows off on the Glass surface. Every placed car starts from these defaults                                                                                                                                                                                                                            |
| M6  | Place it                                 | _Add to scene_ on the toolbar places a car in Harbor at the scene viewport's focus point, activates the Harbor tab and selects the new instance root                                                                                                                                                                                                                                                                          |
| M7  | Go from a placed car to its model        | In Harbor, _Open model_ on `Car3`'s context menu, or the "in Car (model)" link in the Properties pane, opens or activates the `Car.glb` tab with the same node selected                                                                                                                                                                                                                                                       |
| M8  | Reimport after the artist exported again | The status reads "Source changed" and _Reimport_ is enabled; the view, tree and problems update                                                                                                                                                                                                                                                                                                                               |
| M9  | Switch back and forth                    | Clicking the Harbor tab shows Harbor with its selection, camera and history as they were; clicking `Car.glb` shows the car again. Assets loaded for either stay loaded, and no pane moves                                                                                                                                                                                                                                     |
| M10 | Undo a setting                           | Every import-setting change is one step in the tab's history until Save                                                                                                                                                                                                                                                                                                                                                       |
| M11 | Choose the glTF scene                    | `Car.glb` holds two scenes, Night (the default) and Day. The problems list says the model imports Night and names Day as not imported, with a link to the setting; the user chooses Day in the Scene setting and presses Save; the model is reimported with Day's nodes                                                                                                                                                       |
| M12 | Import with the right settings           | _Import > Source model…_ in the Content Explorer picks `Car.fbx`. The Import model dialog reads "FBX 7.4 · centimetres, Z up · 14 nodes · 9 meshes · 6 materials · 11 textures", lists a dropped animation, and shows the import settings; the user points the Glass material at the project's `Glass.omat` and presses Import. The car arrives upright, at the right size, using `Glass.omat`, and opens in the Model Editor |
| M13 | Import several files                     | Three files are picked: each has its own name, folder and settings in the dialog's list, a warning glyph on the one with a problem, and _Apply these settings to all files_ copies the shown file's settings to the others                                                                                                                                                                                                    |

## 3. Decisions

| ID    | Decision                                                                                                                                                                                                                                                                                                    | Reason                                                                                                                                                                                                                                    |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ME-1  | The Model Editor is a document in the workspace's document area, its tab beside the scene's, and it stays in the workspace window like the scene editor                                                                                                                                                     | The engine shows one scene at a time; a model shown in another window would need a second scene in the same frame                                                                                                                         |
| ME-2  | The engine shows the active document: the scene while its tab is active, the model's preview scene while a model tab is active. Switching rebuilds the shown engine scene from its document; loaded assets stay                                                                                             | As RenderScene switches scenes: the native asset loader keeps loaded assets within its budget, so switching is cheap and the engine needs no second scene                                                                                 |
| ME-3  | Opening or activating a model changes no layout: no pane opens, closes, moves or resizes. The docked Scene Explorer and Properties panes show the active document's nodes and selection                                                                                                                     | The user's workspace is theirs; panes that follow the active document are the panes they already arranged                                                                                                                                 |
| ME-4  | The model's structure and values are read-only; only import settings are edited                                                                                                                                                                                                                             | The model is an import output (scene instances SI-2): a change for one placement belongs to a scene, a change for every placement to the import settings                                                                                  |
| ME-5  | The import settings are a panel of the model document, right of the viewport: model-wide groups, then the selected mesh's geometry defaults and the selected texture's settings                                                                                                                             | They are what the document saves (Ctrl+S), as the Texture Editor's settings pane is; the Properties pane stays the inspector of nodes, read-only here as the model is                                                                     |
| ME-6  | Settings take effect on Save, which writes the import settings and reimports. Until then a banner says the view shows the last import                                                                                                                                                                       | The Texture Editor's rule: the view never approximates unsaved settings                                                                                                                                                                   |
| ME-7  | Opening a model opens its Model Editor; _Reimport_ is an explicit command (the toolbar, the Content Explorer's context menu)                                                                                                                                                                                | Double-click reimports today, which shows nothing and redoes work                                                                                                                                                                         |
| ME-8  | The toolbar is the scene editor's `ToolBar` with the model's commands in the same places: Save, Reimport, Add to scene, Browse to asset, the status, Settings                                                                                                                                               | Same control, same positions, same shortcuts: Ctrl+S saves, Ctrl+B browses, and the status sits where the Play status does                                                                                                                |
| ME-9  | The viewport is the scene editor's `Viewport` in its viewing profile: navigation, view mode, overlays, Frame and the status strips stay; editing tools, snapping, layouts and Maximize are hidden                                                                                                           | Nothing in the model can be moved, so tools that move things only add noise; one model needs one view. The profile is a property of the control, not a copy of it                                                                         |
| ME-10 | Import settings are the choices that change what the user gets and differ from one model to the next (section 5.3); what the importer decides from the file (texture formats by use, mipmaps, content hashing) is a rule, not a setting. A model always imports its textures, materials, geometry and scene | A setting the user cannot judge only adds a way to break an import; each setting listed answers a problem real glTF or FBX files have. The scene is required to place the model (scene instances SI-2), and the other products to show it |
| ME-11 | The import settings panel is one control, shown beside the Model Editor's viewport and in the import dialog                                                                                                                                                                                                 | The user meets the same rows, defaults and override bars at the first import and at every later fix, and the two cannot drift apart                                                                                                       |
| ME-12 | The import dialog analyses each file before importing it and shows its facts, its problems and the settings; it has no 3D preview                                                                                                                                                                           | The analysis reads the file's structure without its buffers, so it is fast enough for a dialog; a preview would cost an import, and the Model Editor is that preview, one click away                                                      |

## 4. The document

Models are an asset family with an editor, so they get a family module, as
Materials has ([asset family modules](asset-family-modules.md) section 5.1):
`Oxygen.Editor.Models` owns the model's import settings (`ModelImportSettings`,
the editor's side of the model import record; ContentPipeline cooks and
imports but states no family rule), `ModelDocumentKind`, `ModelContent` and the
model's problems; `Oxygen.Editor.ModelEditor` holds the document's view and
view model, as `Oxygen.Editor.MaterialEditor` does for materials.

`ModelDocumentKind` opens a model source file (`.glb`, `.gltf`, `.fbx`). The
document's content, `ModelContent`, holds:

- the model's import settings, `ModelImportSettings`, read from its model
  import record (`<ProjectRoot>/imports/<source_identity>.import.json`,
  [scene instances](scene-instances.md) NGN14), which Save writes atomically
  and its history records;
- a **preview scene**, built in memory when the document opens and never
  saved: one instance root of the model (a model instance, scene instances
  section 3) at the origin and a preview environment (a sky light, one
  directional light and the ground grid).

Live Preview projects the preview scene like any scene: the instancing, the
picking and the instanced-node selection of
[scene instances](scene-instances.md) do all the work. One Model Editor exists
per model; opening the model again activates its tab. Closing the tab asks to
save unsaved settings.

## 5. Layout

The document fills the document area as the scene editor does: the toolbar on
top, then the viewport and, on its right, the import settings panel, separated
by a splitter. The panel's width and whether it is collapsed are per-user view
state, shared by every model document. Everything else is the workspace's own
panes.

### 5.1 Toolbar

The scene editor's `ToolBar`, with the model's commands where the scene's
equivalents sit:

| Position             | Scene editor                      | Model Editor                                                                                                                                                     |
| -------------------- | --------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| First group          | Save (Ctrl+S), Cook scene         | Save (Ctrl+S): writes the import settings and reimports; Reimport, enabled when the source changed or the last import failed                                     |
| Second group         | Add, Browse to asset, Environment | Add to scene, with its label; Browse to asset (Ctrl+B): the selected node's geometry or material, else the model file                                            |
| Transport and status | Play, Stop, "Ready · Standalone"  | The import status: "Imported", "Source changed", "Importing…", "Import failed", followed by the problem count, a link to the Problems pane filtered to the model |
| End                  | Settings                          | Settings: the same preview settings flyout (frame pacing, grid), shared with the scene editor                                                                    |

The scene's Add menu, Environment and the Play transport do not apply to a
model and are absent, not disabled.

### 5.2 Viewport

The scene editor's `Viewport` control gains a **viewing profile** (`Editing`,
the scene editor's, and `Viewing`), a control property every overlay element
follows. In `Viewing`:

| Viewport element                                  | Viewing profile                                                                                |
| ------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| View camera (Turntable / Fly, direction, lens)    | Shown                                                                                          |
| View mode (Lit, Unlit, Wireframe, …)              | Shown                                                                                          |
| Show (grid, selection outline, icons, statistics) | Shown; the camera preview entry is hidden (a model has no authored cameras to preview through) |
| Frame selection (F, Shift+F)                      | Shown                                                                                          |
| Select, Move, Rotate, Scale tool strip            | Hidden; a click selects                                                                        |
| Transform space, snapping and its increments      | Hidden                                                                                         |
| Viewport layout                                   | Hidden: one perspective view                                                                   |
| Maximize                                          | Hidden: the single view already fills the viewport                                             |
| Viewport settings                                 | Shown                                                                                          |
| Gesture hint and statistics strips                | Shown                                                                                          |
| Transform gizmo and dragging                      | None                                                                                           |

A click selects the node under the cursor (no instance-first rule: the model
is the whole document); Show's selection outline marks it as in scenes. The
banner of ME-6 sits at the viewport's top edge, as the Texture Editor's does.

### 5.3 Import settings panel

One control, `ModelImportSettingsPanel`, shown beside the Model Editor's
viewport and in the import dialog (section 7, ME-11). It is a
`PropertiesExpander` stack of the inspector's controls (`PropertyCard` with its
override bar and reset, `InspectorNumberField`, `InspectorToggleField`,
`AssetPicker`; `Oxygen.Editor.Controls/src/Inspector/`), so every row looks and
behaves as in the Properties pane: a set value carries the override bar and
reset, an unset one shows the importer's default in secondary text. A row that
does not apply to the file is absent, not disabled: the up axis for a glTF
file, the scene for a file with one scene. The panel fills from the source
analysis, which reads a file's structure without loading its buffers (M-NGN2),
so it is complete before the first import.

Each setting answers a problem real files have, and the format differences
decide which rows exist:

| Group              | Setting                                                  | Values, default first                                                                                            | Why it is a setting                                                                                                                                     | Engine                                |
| ------------------ | -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| Source             | Scene                                                    | The file's default scene; any other by name, an unnamed one by index (`#2`)                                      | A glTF file can hold several scenes; shown only then (M11)                                                                                              | NGN12                                 |
| Units and axes     | File                                                     | Read-only: the detected unit, up axis and handedness, such as "centimetres · Z up · right-handed (3ds Max)"      | A model a hundred times too large or lying on its back is the most common FBX import problem; the detected facts explain it before anything is imported | M-NGN2                                |
|                    | Units                                                    | Convert to metres; Keep file units; Custom                                                                       | FBX files from Maya and 3ds Max are in centimetres, from Blender in metres; glTF is in metres by specification                                          | Exists (`unit_policy`)                |
|                    | Scale                                                    | 1.0; shown with Custom                                                                                           | A file authored at the wrong size                                                                                                                       | Exists (`unit_scale`)                 |
|                    | Up axis                                                  | From the file; Y up; Z up. FBX only                                                                              | Some exporters write no axes or wrong ones (`fbx.axis_unknown`); glTF is +Y up by specification, so the row is absent for it                            | M-NGN3                                |
| Geometry           | Normals                                                  | From the file, generate missing; Always recalculate                                                              | Bad exported normals are fixed by recalculating; a glTF primitive without normals stays faceted, as its specification requires, unless recalculated     | Exists (`normals_policy`)             |
|                    | Smoothing angle                                          | 60°; applies to every normal the import generates: missing ones, or all with Always recalculate                  | Generated normals without an angle are all smooth or all faceted, both wrong on hard-surface models                                                     | M-NGN4                                |
|                    | Tangents                                                 | From the file, generate missing (MikkTSpace); Always recalculate                                                 | Normal maps baked in MikkTSpace need MikkTSpace tangents, which glTF requires when a file has none                                                      | Exists (`tangents_policy`)            |
|                    | Keep node pivots                                         | On; Off: node transforms are baked into the meshes, and equal meshes are shared                                  | A change rotates a node about its pivot (opening a door, scene instances S6); baking moves every pivot to the model's origin                            | Exists (`bake_transforms`), M-NGN10   |
|                    | Generate LODs                                            | Off; On, with levels (3, from 1 to 7) and reduction per level (50%)                                              | Most glTF files carry one LOD; LOD groups and `_LODn` meshes of an FBX file are always imported as authored                                             | M-NGN5                                |
| Hierarchy          | Drop empty nodes                                         | Off; On                                                                                                          | Exporters add empty grouping nodes; anchors are taken before pruning, so dropping them keeps node IDs (scene instances NGN2)                            | Exists (`node_pruning`)               |
| Node continuity    | Continued nodes                                          | See below                                                                                                        | A node renamed or moved in the DCC tool keeps its changes (scene instances SI-16)                                                                       | NGN2                                  |
| Materials          | One row per source material                              | Imported; a project material, chosen with the asset picker                                                       | A project usually already has the materials an imported model should use; the replaced material is not produced                                         | M-NGN6                                |
|                    | _Use project materials with the same name_               | A command: fills each row still Imported whose name matches exactly one project material                         | Matching is explicit: a name match is applied only when the user asks, and each filled row can be reset                                                 | Editor                                |
| Textures           | Maximum size                                             | Source size; 4096; 2048; 1024; 512                                                                               | Downloaded models carry 8K textures that cost memory and cook time                                                                                      | M-NGN7                                |
|                    | Compression quality                                      | Default; Fast; High                                                                                              | BC7 encoding trades cook time for quality                                                                                                               | Exists (`bc7_quality`)                |
|                    | Flip normal map green                                    | Off; On                                                                                                          | Normal maps made with the DirectX convention, common in FBX pipelines, light backwards otherwise                                                        | Exists (`flip_normal_green`)          |
| Lights and cameras | Import lights                                            | On; Off                                                                                                          | Props often carry the artist's preview lights                                                                                                           | M-NGN8                                |
|                    | Light intensity scale                                    | 1.0                                                                                                              | FBX intensities have no unit and are converted by a rule (`FbxIntensityToLumens`); one factor corrects a file that comes out too bright or too dark     | M-NGN8                                |
|                    | Range of lights without one                              | 4096 m; glTF only                                                                                                | A glTF light without a range reaches infinitely far                                                                                                     | Exists (`gltf_omitted_light_range_m`) |
|                    | Import cameras                                           | On; Off                                                                                                          | A model's cameras are often turntable or render cameras                                                                                                 | M-NGN8                                |
| Selected mesh      | LOD policy; per surface Visibility and Cast Shadows      | Shown while a mesh node is selected (Model Editor only)                                                          | The asset defaults every placement starts from ([content pipeline](content-pipeline.md) 20.5)                                                           | M-NGN1                                |
| Selected texture   | Maximum size, Compression quality, Flip normal map green | Shown while a texture is chosen from a surface's material (Model Editor only); an override of the Textures group | One texture of a model can need its own size or convention                                                                                              | M-NGN7                                |

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
- **Compressed glTF data** (Draco and meshopt geometry, WebP and KTX2
  textures) is decoded (M-NGN9).

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
become kept changes. Below, every continuity entry, from this and earlier
imports, has a remove button. Entries are import settings: they carry the
override bar until Save, which reimports. Clicking an anchor selects its node
in the Scene Explorer and the viewport. _Reassign_ in a scene's caution box
opens the same flyout ([scene instances](scene-instances.md) 6.4). Mockup:
[model-editor-node-continuity.svg](mockups/model-editor-node-continuity.svg).

### 5.4 The docked panes on a model

- **Scene Explorer:** the model's nodes, rooted at `Car.glb`, from the preview
  scene's instance with the instance root not shown. Rows are the read-only
  instanced rows of scene instances section 6.2, with the Meshes, Lights and
  Cameras filters; the add, folder, rename, cut, copy, paste and delete
  buttons at its foot are disabled with a tooltip naming the model.
- **Properties:** the Scene Inspector on the selected node, read-only, with
  "in Car.glb (model)" in its header. On the root, it shows the model's facts:
  source file, last import, and the counts of nodes, meshes, triangles, LODs,
  materials and textures.
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

**The dialog.** It replaces `SceneImportDialog` and opens on the same
gestures: _Import > Source model…_ (several files may be picked) and invoking
a model source that has never been imported
(`AssetsViewModel.cs:371-381`). Mockup:
[model-import-dialog.svg](mockups/model-import-dialog.svg).

- **Files.** With several files, a list on the left; each file has its own name,
  folder and settings, and a warning glyph when its analysis found a problem.
  _Apply these settings to all files_ copies the shown file's settings, except
  its scene and material rows, which belong to that file.
- **Name and folder.** The existing rows, with the existing replace review
  when a model of that name is already there.
- **Facts.** One line from the analysis: format and version, the detected unit
  and axes, and the counts of scenes, nodes, meshes, materials and textures
  ("FBX 7.4 · centimetres, Z up · 14 nodes · 9 meshes · 6 materials · 11
  textures").
- **Settings.** `ModelImportSettingsPanel` without its Node continuity and
  selection groups, which need a previous import.
- **Before import.** What the analysis found: features that will be dropped
  (an animation, a skin), required extensions that cannot be read, textures
  not found. Each is the diagnostic the import itself will report.
- **Open in the Model Editor.** A check box, on by default for one file; the
  dialog closes when the import starts, and progress shows in the Cooking pane
  as today.

Analysis runs when a file is added to the dialog, through the
`analyze-sources` verb the editor already runs for every model
(`Oxygen.Editor.ContentPipeline/src/ImportToolContentPipelineApi.SourceAnalysis.cs:104`):
it parses a glTF file's JSON without loading its buffers, and an FBX file
without building geometry or decoding images (M-NGN2), so the dialog fills
almost at once. The
dialog has no 3D preview: the Model Editor is the preview, one click away
after the import.

**Ownership.** The panel and the dialog live in `Oxygen.Editor.ModelEditor`,
the model family's UI project. The Content Explorer opens the dialog through
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
(NGN11) and glTF scene selection (NGN12). The import settings need M-NGN1 to M-NGN10. They change the importer,
so they ship in the release of scene instances section 7.15 and share its one
reimport of every model.

| ID      | Capability                   | Today                                                                                                                                                                                                                  | Change                                                                                                                                                                                                                                                                                                                                         |
| ------- | ---------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M-NGN1  | Per-geometry import defaults | The geometry asset already carries them (`GeometryAssetDesc::default_lod_policy`, `SurfaceDesc.flags`, `Data/PakFormat_geometry.h:87-133`), but the model import writes fixed values (`MeshBuildPipeline.cpp:363-374`) | The import settings gain, per geometry anchor, the LOD policy and, per SurfaceId, Visibility and Cast Shadows; the model import writes them into those fields. No geometry format change                                                                                                                                                       |
| M-NGN2  | Source facts in the analysis | `analyze-sources` reports a model job's files and planned outputs; only `inspect-source` reports units and axes, and neither names materials or textures                                                               | A model job of `analyze-sources` reports the detected unit, axes and handedness, the counts of nodes, meshes, materials and textures, the scenes (NGN12) and the names of its materials and textures, all from the metadata parse (`oxygen.source-analysis.schema.json`)                                                                       |
| M-NGN3  | FBX up axis                  | The FBX adapter probes the file's axes and mirrors Y when they are unknown (`FbxAdapter.cpp:1353-1385`); nothing overrides it                                                                                          | `up_axis` (`from-file`, `y`, `z`) in the import manifest job and on the ImportTool; with `y` or `z` the adapter gives ufbx those source axes instead of the probe's                                                                                                                                                                            |
| M-NGN4  | Smoothing angle              | Normals are generated by ufbx's `generate_missing_normals`, with no angle; no smoothing control for glTF                                                                                                               | `normals_smoothing_angle_deg`, used wherever normals are generated or recalculated, in both adapters                                                                                                                                                                                                                                           |
| M-NGN5  | LOD generation               | A glTF geometry has one LOD (`GltfAdapter.cpp:1909-1911`); nothing simplifies meshes                                                                                                                                   | `generate_lods { levels, reduction }`: each geometry with one LOD is simplified with meshoptimizer into further LODs, cooked as authored LODs                                                                                                                                                                                                  |
| M-NGN6  | Material remap               | Every source material produces a material                                                                                                                                                                              | `material_remap`, from source material name to material virtual path: its surfaces reference that material, which the import does not produce; a target that does not exist is a warning, and the source material is produced instead                                                                                                          |
| M-NGN7  | Texture size and overrides   | No maximum size; `texture_overrides` parses, but no adapter reads it                                                                                                                                                   | `max_size` in texture settings, applied before compression; per-texture overrides applied by texture name in both adapters                                                                                                                                                                                                                     |
| M-NGN8  | Lights and cameras           | Imported whenever the scene is (`ImportContentFlags`); FBX intensity converted by a fixed rule; FBX film back ignored                                                                                                  | `import_lights`, `import_cameras` and `light_intensity_scale`; an FBX camera takes its vertical field of view from film height and focal length when the file states them                                                                                                                                                                      |
| M-NGN9  | Compressed glTF data         | Draco, meshopt, WebP and KTX2 are unread extensions: the geometry or texture they hold is missing                                                                                                                      | The glTF adapter decodes `KHR_draco_mesh_compression`, `EXT_meshopt_compression`, `EXT_texture_webp` and `KHR_texture_basisu`                                                                                                                                                                                                                  |
| M-NGN10 | Pivots kept by default       | `bake_transforms` defaults to true in `ImportOptions`; the editor already writes false                                                                                                                                 | The engine default becomes false, so an ImportTool import keeps pivots as an editor import does                                                                                                                                                                                                                                                |
| M-ED1   | Model document               | Double-click reimports a model ([AssetsViewModel.cs:369](../../../projects/Oxygen.Editor.ContentExplorer/src/Panes/Assets/AssetsViewModel.cs))                                                                         | `Oxygen.Editor.Models` (family module: import settings, `ModelDocumentKind`, `ModelContent`, problems) and `Oxygen.Editor.ModelEditor` (UI); opening a model opens it; _Reimport_ becomes a command (ME-7)                                                                                                                                     |
| M-ED2   | Shown document               | Live Preview shows the scene document opened last                                                                                                                                                                      | Live Preview shows the active scene or model document and rebuilds on activation (section 6)                                                                                                                                                                                                                                                   |
| M-ED3   | Panes follow the document    | The Scene Explorer and the Properties pane follow the scene document                                                                                                                                                   | They follow the active scene or model document, with the read-only model presentation of section 5.4; no layout change (ME-3)                                                                                                                                                                                                                  |
| M-ED4   | Every import setting         | The model import record holds the import settings, written with defaults (`ModelImportSettings.DefaultSettings`); no surface chooses them                                                                              | `ModelImportSettings` reads and writes the model import record (scene instances NGN14), with the settings of section 5.3, the node continuity and the published anchors. The product reads no older format: the few existing sidecars become records by hand outside the product, keeping their source identity (scene instances section 7.15) |
| M-ED5   | Kept import problems         | Import diagnostics reach the operation results and the Problems pane once                                                                                                                                              | The last import's diagnostics are retained with the model's publication and read when the document opens                                                                                                                                                                                                                                       |
| M-ED6   | Viewport viewing profile     | The `Viewport` control shows every editing overlay                                                                                                                                                                     | `Viewport` gains its `Editing` and `Viewing` profiles (section 5.2), with their own tests; the scene editor keeps `Editing`                                                                                                                                                                                                                    |
| M-ED7   | Tab stays in its window      | Documents other than the scene can be torn out                                                                                                                                                                         | `DocumentEditorDescriptor` (`Oxygen.Editor.Shell/src/Documents/DocumentEditorDescriptor.cs`) gains `CanTearOut`; the model document's provider sets it false                                                                                                                                                                                   |
| M-ED8   | Import dialog                | `SceneImportDialog` asks the name, the folder and the replace choice                                                                                                                                                   | The Import model dialog of section 7 in `Oxygen.Editor.ModelEditor`, opened through the `IModelImportPrompt` port; the chosen settings are written into the new model import record                                                                                                                                                            |
| M-ED9   | Import settings panel        | None                                                                                                                                                                                                                   | `ModelImportSettingsPanel` (section 5.3), hosted by the model document and the import dialog, filled from the source analysis                                                                                                                                                                                                                  |

## 9. Exit gates

| Gate | Check                                                                                                                                                    |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MG1  | Double-clicking `Car.glb` opens its tab with the car framed and its nodes in the Scene Explorer; nothing is reimported, and no pane moves or resizes     |
| MG2  | Switching between Harbor and `Car.glb` keeps each document's selection, camera and history, and reloads no geometry that is still in the loader's budget |
| MG3  | Setting a custom scale and saving reimports the car; Harbor's five cars show the new size                                                                |
| MG4  | A Distance LOD policy and a surface's Cast Shadows set on `Body` become the asset defaults every placed car uses, unless a scene changes them            |
| MG5  | A model with a missing texture lists the problem with its material and node on every opening of the document                                             |
| MG6  | _Add to scene_ places the car in Harbor, activates Harbor and selects the new instance root                                                              |
| MG7  | The viewport shows no editing tool, snapping, layout or Maximize control, and dragging in it never moves a node                                          |
| MG8  | Importing an FBX file in centimetres and Z up shows "centimetres, Z up" in the dialog before the import, and the model arrives at its size and upright   |
| MG9  | A source material pointed at a project material in the dialog is used by each of its surfaces, and no material is produced for it                        |
| MG10 | A glTF file whose geometry uses meshopt compression imports its geometry                                                                                 |
