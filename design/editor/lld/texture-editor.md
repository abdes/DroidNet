# Texture Editor LLD

Status: `approved 2026-10-21; delivered by ED-M09A`

Last Updated: 2026-10-21

## 1. Purpose

Let the user bring an image into a project in one step, tune how the cooker
cooks it, and inspect exactly what the cooker produced. A texture is a
descriptor (`.otex.json`) that names its source images and the cooker's
settings. Importing creates that descriptor with settings chosen from the image
and its name, and opens it in the Texture Editor: a document tab with every
setting the cooker accepts and a view of the cooked texels, mip by mip, layer
by layer and face by face. Showing the texture in use (lit, on a material, as
a sky) is the job of the material and scene views, not this editor.

Mockups (dark theme; the implementation matches them, and where a mockup and
this text differ, this text wins):

- [mockups/texture-editor.svg](mockups/texture-editor.svg): a 2D texture.
- [mockups/texture-editor-cube.svg](mockups/texture-editor-cube.svg): a cube,
  its face layouts and its six face sources.

## 2. PRD Traceability

| ID        | Coverage                                                                                                                                       |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `REQ-049` | New: users import an image as a texture with detected settings, edit every texture setting the cooker supports, and inspect the cooked texels. |
| `REQ-006` | Settings change through recorded edits; Undo, Redo and dirty state work as in every document.                                                  |
| `REQ-022` | Invalid settings, failed inspections, cooks and texture reads produce visible results.                                                         |

## 3. Architecture Links

- [asset-family-modules.md](asset-family-modules.md): Textures owns import
  authoring; the document kind comes with the Texture Editor.
- [document-kinds.md](document-kinds.md) decision 7: a texture document edits
  its descriptor.
- [editor-module-architecture.md](editor-module-architecture.md): the
  `Oxygen.Editor.TextureEditor` feature module.
- [content-cooking-workflows.md](content-cooking-workflows.md) and
  [editor-documents.md](editor-documents.md): cook on change after a save.
- [workspace-title-bar.md](workspace-title-bar.md): File > Import texture or
  image….

## 4. User Scenarios

1. **Import.** The user chooses Import texture or image… and picks
   `Bricks_N.png` in the Content Explorer's `Textures` folder. The image is
   copied there, `Bricks_N.otex.json` is created with the Normal preset because
   of the `_N` suffix, and the Texture Editor opens on it. With cook on change
   on, the texture cooks and the view shows the cooked texels.
2. **Change a setting.** The user sets the mip filter to Lanczos. The field
   shows the override and a reset button; the document is dirty; the view's
   banner says it shows the last cook. After Save, the texture cooks and the
   view updates.
3. **See defaults.** Every setting the user has not set shows the value the
   cooker will use and where it comes from ("BC7 sRGB, from preset Albedo"),
   because the cooker's tool reports it.
4. **Inspect the result.** The user toggles the alpha channel, steps to mip 3,
   zooms to 400 % to see BC7 blocks, and reads the cooked format, size, mip
   count and memory.
5. **Check a cube.** The user imports `Sky.hdr`, a 2:1 panorama; the HDR
   Environment preset and a 512 px face size are chosen. The view shows the six
   cooked faces as a horizontal cross with their labels and seam lines; the
   user switches to a vertical strip, then to the +Y face alone, and adjusts
   the exposure to read the bright sun.
6. **Fix a face.** A cube imported from six files shows its six face slots;
   the +Z slot holds `Room_back.png` where `Room_front.png` belongs. The user
   swaps the two slots; the descriptor now names each face's file, and after
   Save the cooked cube is correct.
7. **Check a source layout.** For a cross image, the Source group shows the
   image with each detected face outlined and labelled, so a wrong layout
   detection is visible before cooking.
8. **Invalid settings.** The user picks BC7 with quality None. The field shows
   the cooker's error; Save still works, the cook reports the same error, and
   the view keeps the last good cook.
9. **Replace the source.** Change source… picks another image; it is copied
   into the texture's folder and the descriptor names it; the old file stays.
10. **Authored layers and mips.** The user adds source images for array layers
    or explicit mips in the Sources table, each with its layer, mip and slice.
11. **Undo.** Every change above is one step in the document's history.

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
- **Defaults are resolved in the cooker** (`MapSettingsToTuning`): a preset sets
  intent, color space, formats, mip policy and filter, BC7 quality and HDR
  handling; an intent without a preset sets color space and format but leaves
  mips at none.
- **Formats.** Only BC7 encodes; formats are RGBA8 (UNorm, sRGB), BC7 (UNorm,
  sRGB), RGBA16F and RGBA32F. The height intent and preset default to R16UNorm,
  which the cooker cannot write.
- **Six-file cubes** are found only by face-name suffixes next to `source`
  (`DiscoverCubeFacePaths`); a descriptor cannot name a face's file.
- **Managed model.** `TextureSource` (Managed.Assets) models `name`,
  `virtual_path`, `source`, `sources[]`, `intent`, `decode.color_space`,
  `output.format` and `cube`, and keeps every other key on a read and write.
- **Import today.** `TextureImport` (Textures) copies the image and writes the
  modelled keys, detecting the cube layout from the image's aspect; the Content
  Explorer's dialog collects name, folder, shape, layout, face size, intent,
  color space and format.
- **Cooked texels.** A cooked texture's payload in `textures.data` (loose or in
  a pak) is a `TexturePayloadHeader` followed by one `SubresourceLayout` per
  mip, layer and face, then the data. Managed code reads only the texture's
  record (type, format, size, layers); nothing reads the payload, and nothing
  in the repository decodes BC7.
- **ImportTool.** `inspect-source` writes a JSON report about a model source
  without cooking; nothing does that for a texture.

## 6. Decisions

### Import

1. **Import creates and opens.** Import texture or image… (`Content.ImportTexture`)
   asks for image files, copies each into the Content Explorer's current
   folder, writes its descriptor and opens the first in the Texture Editor.
   There is no import dialog; `TextureImportDialog` is deleted.
2. **The preset comes from the image.** The importer writes a `preset` and
   leaves the rest to the cooker:
   - a name ending in `_N`, `_Nrm` or `_Normal` gives `normal`; `_ORM`, `orm`;
     `_R` or `_Roughness`, `roughness`; `_M` or `_Metallic`, `metallic`;
     `_AO`, `ao`; `_E` or `_Emissive`, `emissive`; `_H` or `_Height`,
     `height`; any other name, `albedo`;
   - an HDR image (`.hdr`, `.exr`) gives `hdr-env` when it is a 2:1 panorama
     (with `equirect_to_cube` and a face size of a quarter of its width,
     rounded to a multiple of 256) and `hdr-probe` otherwise;
   - a strip or cross image detected by today's aspect rules gets
     `cube_layout`; six face files named by the face suffixes get `cubemap`
     and their explicit face sources (decision 15).
     The rules live in `TextureImport`, with their table in its README.

### Document and settings

3. **A texture is a document.** `TextureDocumentKind` (Textures) opens a
   `.otex.json` as a document whose content is a `TextureSource`; every edit is
   recorded in the document's history and Save writes the descriptor
   atomically.
4. **The model covers the whole descriptor.** `TextureSource` gains every key of
   section 5, and the face sources of decision 15, as typed properties, null
   meaning "not set". Unknown keys keep round-tripping.
5. **The cooker's tool resolves effective settings.** The ImportTool gains
   `inspect-texture <descriptor> [--base-dir <folder>] --output <json>`:
   without cooking, it reports every setting's effective value and its origin
   (set, preset, intent or default), the validation errors with the setting
   each concerns, the source images' facts (size, channels, bit depth, HDR, the
   detected layout and each face's region in a strip or cross, the discovered
   face files), and the predicted output (format, size, mip count, bytes). The
   content pipeline exposes it as `IEngineContentPipelineApi.InspectTextureAsync`.
   The editor never copies the cooker's tables.
6. **Each field shows its effective value.** A set field shows its value with a
   reset button that removes the key; an unset field shows the effective value
   in secondary text with its origin. After each edit, debounced, the editor
   writes the document's content to the system's temporary folder and inspects
   it with `--base-dir` set to the texture's folder, so nothing is written into
   the project.
7. **Settings are grouped as the cooker groups them:** Source, Usage (preset,
   intent), Shape (2D or cube and the cube mode: panorama, strip or cross, six
   files), Color (color space, flip Y, flip normal green, force RGBA), Mips
   (policy, maximum, filter, renormalize), Output (format, data format, BC7
   quality, packing), HDR (handling, exposure, bake) and Advanced (name,
   virtual path, content hashing). A group that does not apply (HDR for an LDR
   image) is collapsed with the reason, not hidden.
8. **Sources.** The primary source has Change source…; the Sources table lists
   extra files with layer, mip and slice, with Add and Remove. A cube's Source
   group shows instead what decision 13 describes. A changed or added file is
   copied into the texture's folder.

### Texture view

9. **The view shows the cooked texels, decoded on the CPU.** It reads the
   texture's last published cook (the payload of section 5) through a new
   managed payload reader in Managed.Assets, decodes the chosen subresource on
   the CPU, and shows it in a WinUI view. BC decoding is specified bit-exactly,
   so the view shows exactly the texels the GPU samples at that mip with point
   filtering. The engine renders nothing for this editor; the texture in use is
   the material and scene views' job.
10. **Decoding uses a managed library.** A vetted MIT-licensed .NET decoder for
    BC1 to BC7, including BC6H (BCnEncoder.NET is the candidate; its license
    and version are checked before adoption), behind a `TextureTexels` decoder
    in Managed.Assets, so later formats need no new decoding code. RGBA8,
    RGBA16F and RGBA32F need no decoding.
11. **View tools,** in the view's toolbar: R, G, B and A toggles; mip; array
    layer or cube face; exposure (HDR textures); zoom (Fit, 100 %, wheel, with
    nearest-neighbour magnification so texels and blocks stay visible) and pan;
    checker or solid background. Channel masking, exposure and the HDR view
    transform are applied in managed code to the decoded mip, once per change.
    An info strip below shows the source size, cooked format, size, mip count
    and memory.
12. **The view follows the cook.** While the document is dirty, or its cook is
    out of date, running or failed, a banner says the view shows the last cook,
    with Save and cook or Cook. Unsaved settings are never shown by
    approximation.

### Cubes

13. **A cube is edited as six faces.** For a cube the view lays the six cooked
    faces out as a horizontal cross (default), vertical cross, horizontal strip,
    vertical strip, a 3×2 grid, or a single face, each labelled +X to -Z, with
    optional seam lines so a misoriented face stands out. The layout is a viewing
    choice, independent of the source's. The Source group shows how the source
    maps to faces: for a strip or cross image, the image with each face's region
    outlined and labelled (from the inspection); for a panorama, the source image;
    for six files, six labelled face slots with each file's name and thumbnail and
    a missing face flagged.
14. **No sky preview.** The cube is shown as faces only; seeing it as a sky is
    the scene environment's job.
15. **Faces can be assigned explicitly.** The cooker gains explicit face
    sources: `sources[]` entries with a `face` (`+x`, `-x`, `+y`, `-y`, `+z`,
    `-z`) for a cube, used instead of name discovery when present; name
    discovery stays the default. In the six face slots the user drops, picks or
    swaps files, which writes face sources (owner's decision, 2026-10-21).

### Placement and problems

16. **Problems are shown where they are.** Inspection errors appear on their
    field and in the header's count; a cook's failures go to the Problems panel
    through the cook's results.
17. **The editor is a document tab,** like the Material Editor, with no limit on
    how many are open.

## 7. Engine And Tool Changes

- **ImportTool `inspect-texture`** (decision 5), its report's JSON schema in the
  ImportTool README, and native tests over every preset, intent, error and
  source kind.
- **Explicit cube face sources** (decision 15): schema, request builder and cube
  job, with native tests that explicit faces win over discovery and that a
  missing or duplicated face is an error.
- **R16UNorm output.** The height intent and preset default to R16UNorm; the
  cooker gains R16UNorm output (the first channel of the source) and the
  descriptor's format list gains `r16`, so height maps keep 16-bit precision
  (owner's decision, 2026-10-21). Native tests of the conversion.

No engine runtime or renderer change: the editor reads the cooked payload. BC4, BC5 and BC6H, a maximum size, alpha handling and channel packing are cooker features of their own, tracked by [#87](https://github.com/abdes/DroidNet/issues/87); the editor shows them through `inspect-texture` when they land.

## 8. Modules And Types

| Type                       | Module          | Role                                                                     |
| -------------------------- | --------------- | ------------------------------------------------------------------------ |
| `TextureSource` (extended) | Managed.Assets  | The whole descriptor, face sources included (decision 4).                |
| `TexturePayloadReader`     | Managed.Assets  | Reads a cooked texture's payload header, layouts and subresource bytes.  |
| `TextureTexels`            | Managed.Assets  | Decodes a subresource to RGBA (decision 10).                             |
| `TextureImport` (reworked) | Textures        | Copy and write with the detected preset (decision 2); no dialog.         |
| `TextureDocumentKind`      | Textures        | The texture document (decision 3).                                       |
| `TextureInspection`        | ContentPipeline | The `inspect-texture` report as typed data, through the pipeline API.    |
| `TextureEditorModule`      | TextureEditor   | Registers the editor provider and view for the texture kind.             |
| `TextureEditorViewModel`   | TextureEditor   | Groups, fields, effective values, sources, faces, inspection and banner. |
| `TextureViewViewModel`     | TextureEditor   | The view's tools, the decoded image, cube layouts and the info strip.    |
| `TextureEditorView`        | TextureEditor   | The layout of the mockups.                                               |

The Content Explorer's import command calls `TextureImport` and opens the result
through the document service.

## 9. Operation Results And Diagnostics

- Import: the existing texture import codes, with the detected preset in the
  result's message.
- Inspection failure (the tool did not run or its report is unreadable): a
  failed result, kind `Texture.Inspect`, code `OXE.TEXTURE.INSPECT_FAILED`,
  domain `AssetImport`; the fields keep their last effective values.
- Texel read failure (no published cook, unreadable payload, unsupported
  format): the view shows the reason and publishes a failed result, kind
  `Texture.ReadTexels`, code `OXE.TEXTURE.TEXELS_UNREADABLE`, domain `AssetCook`.
- Cook failures report through the cook, unchanged.

## 10. UI Design

- **Layout.** The texture view fills the left of the tab; the settings pane is
  on the right (320 px default, resizable). A header row shows the texture's
  name, kind and preset, its cook state chip (Up to date, Out of date, Cooking,
  Failed), the problem count and Cook.
- **Texture view.** The toolbar of decision 11, with the cube layout picker for
  a cube; the banner of decision 12 across its top when it applies; the info
  strip below.
- **Settings pane.** Collapsible groups of label and editor rows in the editor's
  property style. A set value shows normally with a reset button; an effective
  value shows in secondary text with its origin; a field with an error shows it
  under the field.
- **Cube sources.** Six labelled face slots in a cross, each with a thumbnail
  and file name, accepting a dropped or picked file, with swap by dragging one
  slot onto another.

## 11. Dependency Rules

- Textures owns import and the document kind and references no editor UI.
- Managed.Assets owns reading and decoding cooked texels.
- TextureEditor is a feature: it references Shell, Textures, Managed.Assets and
  ContentPipeline (the inspection and the cook state); it does not reference
  Runtime, LivePreview or Interop.
- Only the ImportTool knows the cooker's defaults.

## 12. Test Strategy

- Native tests: `inspect-texture`, explicit face sources and R16UNorm, as in
  section 7.
- Managed.Assets tests: every descriptor key round-trips and unknown keys
  survive; the payload reader over cooked fixtures (2D with mips, array, cube,
  loose and pak); decoding against reference texels for each format.
- Textures tests: the preset detection table, HDR and cube detection, face
  sources for six files, the copy.
- ContentPipeline tests: the inspection report parsed from recorded JSON.
- TextureEditor tests: set and effective values with origins; reset removes a
  key; edits are recorded and undone; the banner follows dirty and cook state;
  sources and faces are assigned and swapped; channel masks, mips, faces and
  layouts select the right texels; exposure transforms HDR texels.
- All of it runs without a GPU. The editor in the running app (scenarios 1 to 11) waits for the owner's acceptance.
