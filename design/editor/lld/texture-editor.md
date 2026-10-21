# Texture Editor LLD

Status: `draft for review`

Last Updated: 2026-10-21

## 1. Purpose

Let the user bring an image into a project in one step and then tune how the
engine cooks it, seeing the result as the engine renders it. A texture is a
descriptor (`.otex.json`) that names its source images and the cooker's
settings. Importing creates that descriptor with settings chosen from the image
and its name, and opens it in the Texture Editor. The editor is a document tab
with every setting the cooker accepts and a preview the embedded engine renders
from the cooked texture.

Mockup (dark theme; the implementation matches it, and where it and this text
differ, this text wins): [mockups/texture-editor.svg](mockups/texture-editor.svg).

## 2. PRD Traceability

| ID        | Coverage                                                                                                                                                                    |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `REQ-049` | New: users import an image as a texture with detected settings, edit every texture setting the cooker supports, and see the cooked texture rendered by the embedded engine. |
| `REQ-006` | Settings change through recorded edits; Undo, Redo and dirty state work as in every document.                                                                               |
| `REQ-022` | Invalid settings, failed inspections, cooks and previews produce visible results.                                                                                           |

## 3. Architecture Links

- [asset-family-modules.md](asset-family-modules.md): Textures owns import
  authoring; the document kind comes with the Texture Editor.
- [document-kinds.md](document-kinds.md) decision 7: a texture document edits
  its descriptor.
- [editor-module-architecture.md](editor-module-architecture.md): the
  `Oxygen.Editor.TextureEditor` feature module.
- [content-cooking-workflows.md](content-cooking-workflows.md) and
  [editor-documents.md](editor-documents.md): cook on change after a save.
- [live-preview.md](live-preview.md): the engine surfaces and views the preview
  uses.
- [workspace-title-bar.md](workspace-title-bar.md): File > Import texture or
  image….

## 4. User Scenarios

1. **Import.** The user chooses Import texture or image… and picks
   `Bricks_N.png` in the Content Explorer's `Textures` folder. The image is
   copied there, `Bricks_N.otex.json` is created with the Normal preset because
   of the `_N` suffix, and the Texture Editor opens on it. With cook on change
   on, the texture cooks and the preview shows it.
2. **Change a setting.** The user sets the mip filter to Lanczos. The field
   shows the override and a reset button; the document is dirty; the preview
   banner says it shows the last cooked result. After Save, the texture cooks
   and the preview updates.
3. **See defaults.** Every setting the user has not set shows the value the
   cooker will use and where it comes from ("BC7 sRGB, from preset Albedo"),
   because the engine resolves it.
4. **Inspect the result.** The user toggles the alpha channel, steps to mip 3,
   zooms to 100 % and reads the cooked format, size, mip count and memory.
5. **HDR environment.** The user imports `Sky.hdr`, a 2:1 panorama. The HDR
   Environment preset and a 512 px cube face are chosen; the preview shows the
   cube as a 3D sky the user can orbit, with an exposure slider, or as an
   unfolded cross.
6. **Invalid settings.** The user picks BC7 with quality None. The field shows
   the cooker's error and Save still works, but the cook reports the same
   error and the preview keeps the last good result.
7. **Replace the source.** The user chooses Change source… and picks another
   image; it is copied into the texture's folder, the descriptor names it, and
   the old file stays where it was.
8. **Authored layers and mips.** The user adds source images for array layers
   or explicit mips in the Sources table, each with its layer, mip and slice.
9. **Undo.** Every change above is one step in the document's history.

## 5. Integration Baseline

What exists today:

- **Descriptor.** The cooker accepts `name`, `virtual_path`, `content_hashing`,
  `source`, `sources[]` (`file`, `layer`, `mip`, `slice`), `preset` (18
  presets), `intent` (12 intents), `decode` (`color_space`, `flip_y`,
  `force_rgba`, `flip_normal_green`), `mips` (`policy` none/full/max,
  `max_mips`, `filter` box/kaiser/lanczos, `renormalize`), `output` (`format`,
  `data_format`: rgba8, rgba8 sRGB, bc7, bc7 sRGB, rgba16f, rgba32f;
  `bc7_quality` none/fast/default/high; `packing_policy` d3d12/tight), `hdr`
  (`handling`, `exposure_ev`, `bake_hdr`) and `cube` (`cubemap`,
  `equirect_to_cube`, `cube_face_size` a multiple of 256, `cube_layout`
  auto/hstrip/vstrip/hcross/vcross). Its schema is closed.
- **Defaults are resolved in the cooker.** A preset sets intent, color space,
  formats, mip policy and filter, BC7 quality and HDR handling; an intent
  without a preset sets color space and format but leaves mips at none. The
  resolution lives in the cooker only.
- **Compression.** Only BC7 encodes; formats are RGBA8 (UNorm, sRGB), BC7
  (UNorm, sRGB), RGBA16F and RGBA32F. The height intent and preset default to
  R16UNorm, which the cooker cannot write.
- **Managed model.** `TextureSource` (Managed.Assets) models `name`,
  `virtual_path`, `source`, `sources[]`, `intent`, `decode.color_space`,
  `output.format` and `cube`; it keeps every other key on a read and write. The
  writer validates against the engine's schema.
- **Import today.** `TextureImport` (Textures) copies the image and writes the
  modelled keys, detecting the cube layout from the image's aspect. The Content
  Explorer's import dialog collects name, folder, shape, layout, face size,
  intent, color space and format.
- **Cooked facts.** `TextureResourceRecord` reads a cooked texture's type,
  format, width, height and layers; the record also holds mips, depth, size and
  compression, which nothing reads.
- **Cook.** A saved descriptor is a cook input; cook on change cooks it after
  its quiet window when the project's setting is on.
- **ImportTool.** `inspect-source` writes a JSON report about a model source
  without cooking; nothing does that for a texture.
- **Engine views.** The editor creates engine views on surfaces it owns per
  document; every view renders a scene, and no view renders a texture. A cube
  texture can be shown as a scene's sky.

## 6. Decisions

### Import

1. **Import creates and opens.** Import texture or image… (`Content.ImportTexture`)
   asks for image files, copies each into the Content Explorer's current
   folder, writes its descriptor and opens the first in the Texture Editor.
   There is no import dialog; `TextureImportDialog` is deleted.
2. **The preset comes from the image.** The importer writes a `preset` and
   nothing else it can leave to the cooker:
   - a name ending in `_N`, `_Nrm` or `_Normal` gives `normal`; `_ORM` gives
     `orm`; `_R` or `_Roughness`, `roughness`; `_M` or `_Metallic`,
     `metallic`; `_AO`, `ao`; `_E` or `_Emissive`, `emissive`; `_H` or
     `_Height`, `height`; any other name, `albedo`;
   - an HDR image (`.hdr`, `.exr`) gives `hdr-env` when it is a 2:1 panorama
     (with `equirect_to_cube` and a face size of a quarter of its width,
     rounded to a multiple of 256) and `hdr-probe` otherwise;
   - a strip or cross image detected by today's aspect rules gets
     `cube_layout`, and six face files named by the cooker's face suffixes get
     `cubemap`.
     The rules live in `TextureImport`, with a table in its README.

### Document and editor

3. **A texture is a document.** `TextureDocumentKind` (Textures) opens a
   `.otex.json` as a document whose content is a `TextureSource`; every edit is
   recorded in the document's history and Save writes the descriptor
   atomically.
4. **The model covers the whole descriptor.** `TextureSource` gains every key of
   section 5 as typed properties, with null meaning "not set". The reader and
   writer keep round-tripping keys they do not know.
5. **The engine resolves effective settings.** The ImportTool gains
   `inspect-texture <descriptor> [--base-dir <folder>] --output <json>`:
   without cooking, it reports
   the effective value of every setting and where it comes from (set, preset,
   intent or default), the validation errors with the setting they concern, the
   source images' facts (size, channels, bit depth, HDR, detected layout), and
   the predicted output (format, size, mip count, bytes). The content pipeline
   exposes it as `IEngineContentPipelineApi.InspectTextureAsync`. The editor
   never copies the cooker's tables.
6. **Each field shows its effective value.** A field the descriptor sets shows
   its value with a reset button that removes the key; a field it does not set
   shows the effective value in secondary text with its origin. The editor
   inspects the document's current content after each edit, debounced: it
   writes the content to a file in the system's temporary folder and passes the
   texture's folder as `--base-dir`, against which the tool resolves the source
   paths, so nothing is written into the project.
7. **Settings are grouped as the cooker groups them:** Source, Usage (preset,
   intent), Shape (2D or cube, and the cube mode: panorama, strip or cross,
   six files), Color (color space, flip Y, flip normal green, force RGBA), Mips
   (policy, maximum, filter, renormalize), Output (format, data format, BC7
   quality, packing), HDR (handling, exposure, bake), and Advanced (name,
   virtual path, content hashing). Groups that do not apply (HDR for an LDR
   image) are collapsed, not hidden.
8. **Sources are edited as a table.** The primary source has Change source…;
   the Sources table lists extra files with layer, mip and slice, and Add and
   Remove. A changed or added file is copied into the texture's folder.
9. **The preview shows the cooked texture.** It renders the last published
   cook of this texture. While the document is dirty, or its cook is out of
   date, running or failed, a banner says so and offers Cook when cook on
   change is off. Unsaved settings are never previewed by approximation.
10. **The engine renders the preview.** The engine gains a texture preview view
    (section 7): a view on an editor surface that draws a cooked texture by
    virtual path, flat or, for a cube, as a sky around an orbiting camera. The
    Texture Editor owns one surface per open texture and releases it on close.
11. **Preview tools**, in the preview's toolbar: R, G, B and A toggles; mip (Auto
    or a level); array slice or cube face; for a cube, 3D sky or unfolded cross;
    exposure for HDR; zoom (Fit, 100 %, wheel) and pan; checker or solid
    background. The info strip below shows source size, cooked format, size, mip
    count and memory, read from the cooked record.
12. **Problems are shown where they are.** Inspection errors appear on their
    field and in the editor's header count; a cook's failures go to the
    Problems panel through the cook's results, as for every asset.
13. **The editor is a document tab** of the workspace, like the Material Editor,
    with no limit on how many are open.

### Engine fixes this design needs

14. **The height defaults are fixed in the cooker.** The height intent and
    preset default to a format the cooker writes. The engine owner chooses
    between adding R16UNorm output or changing the default to a supported
    format; until then the editor shows the inspection's error for a height
    texture without an explicit format.

## 7. Engine And Tool Changes

- **ImportTool `inspect-texture`** (decision 5), with the report's JSON schema in
  the ImportTool README and native tests over every preset, intent and error.
- **Cooked record.** The managed `TextureResourceRecord` reads mips, depth,
  size and compression as well.
- **Texture preview view.** `IEngineService.CreateTexturePreviewAsync(surface,
TexturePreviewConfig)` and `UpdateTexturePreviewAsync`, where
  `TexturePreviewConfig` holds the virtual path, the mode (flat, cube sky, cube
  cross), the channel mask, mip, slice or face, exposure EV, background, zoom
  and pan. Natively, the editor module creates a view that loads the texture
  through the content loader from the mounted cooked roots, reloads it when a
  publication replaces it, and draws it with a preview pass (flat and cross)
  or a private scene with the texture as its sky (cube sky). Its design is
  written with the Vortex rules in `design/oxygen` before its code.
- **Height default** (decision 14).

## 8. Modules And Types

| Type                       | Module          | Role                                                                     |
| -------------------------- | --------------- | ------------------------------------------------------------------------ |
| `TextureSource` (extended) | Managed.Assets  | The whole descriptor (decision 4).                                       |
| `TextureImport` (reworked) | Textures        | Copy and write with the detected preset (decision 2); no dialog request. |
| `TextureDocumentKind`      | Textures        | The texture document (decision 3).                                       |
| `TextureInspection`        | ContentPipeline | The `inspect-texture` report as typed data, through the pipeline API.    |
| `TextureEditorModule`      | TextureEditor   | Registers the editor provider and view for the texture kind.             |
| `TextureEditorViewModel`   | TextureEditor   | Groups, fields, effective values, sources, inspection and banner.        |
| `TexturePreviewViewModel`  | TextureEditor   | The preview's tools, its surface and view, and the info strip.           |
| `TextureEditorView`        | TextureEditor   | The layout of the mockup.                                                |

The Content Explorer's import command calls `TextureImport` and opens the
result through the document service.

## 9. Operation Results And Diagnostics

- Import: the existing texture import codes, plus the detected preset in the
  result's message.
- Inspection failure (the tool did not run or its report is unreadable): a
  failed result, kind `Texture.Inspect`, code `OXE.TEXTURE.INSPECT_FAILED`,
  domain `AssetImport`; the fields keep their last effective values.
- Preview failure (no surface, the texture does not load): the preview shows the
  reason and publishes a failed result, kind `Texture.Preview`, code
  `OXE.TEXTURE.PREVIEW_FAILED`, domain `RuntimeView`.
- Cook failures report through the cook, unchanged.

## 10. UI Design

- **Layout.** The preview fills the left of the tab; the settings pane is on the
  right (320 px default, resizable). A header row above both shows the texture's
  name, its cook state chip (Up to date, Out of date, Cooking, Failed), the
  problem count and Cook.
- **Preview.** A toolbar above it with the tools of decision 11; the banner of
  decision 9 across its top when it applies; the info strip below it.
- **Settings pane.** Collapsible groups of label and editor rows, in the
  editor's property style. A set value shows normally with a reset button; an
  effective value shows in secondary text with its origin; a field with an
  error shows the error under it.
- **Sources table.** File, layer, mip and slice columns, with Add, Remove and
  Change source….

## 11. Dependency Rules

- Textures owns import and the document kind and references no editor UI.
- TextureEditor is a feature: it references Shell, Textures, ContentPipeline (the
  inspection and the cook state) and LivePreview or Runtime for its preview, as
  the architecture allows a feature that shows engine views.
- Only the ImportTool knows the cooker's defaults.

## 12. Test Strategy

- Native tests: `inspect-texture` over every preset and intent, overrides,
  every validation error and source facts; the preview view's load, reload on
  publication and parameters.
- Managed.Assets tests: every descriptor key round-trips, unknown keys survive.
- Textures tests: the preset detection table, HDR and cube detection, the copy.
- ContentPipeline tests: the inspection report parsed from recorded JSON.
- TextureEditor tests: fields show set and effective values with origins;
  reset removes a key; edits are recorded and undone; the banner follows dirty
  and cook state; sources are added, removed and copied.
- The editor in the running app (scenarios 1 to 9) waits for the owner's
  acceptance.

## 13. Open Questions For The Owner

1. **Engine scope.** The preview needs a new engine view (section 7), and the
   effective settings need a new ImportTool command. Both are engine and tool
   work inside this editor feature. Approve them, or choose a preview without
   the engine (decoding the source image in WinUI), which would not show the
   cooked result.
2. **Height default** (decision 14): add R16UNorm output to the cooker, or
   change the height default to a supported format?
