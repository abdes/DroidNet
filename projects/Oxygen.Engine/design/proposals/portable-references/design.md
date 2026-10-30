# Portable references: design

Status: proposal

Last updated: 2026-10-30

This is the component design of the [portable references proposal](README.md).
The proposal states the rules (R1 to R11) and the scenarios they serve; this
document says, per module, which types carry each rule, what they replace and
which old forms disappear. The [implementation plan](plan.md) orders the work
into slices and holds the debt ledger.

Every statement about today's code was read in the code. Paths are relative to
`projects/`; `Engine/` is `Oxygen.Engine/src/Oxygen/`.

## Contents

- [Shared contracts](#shared-contracts)
- [Engine](#engine)
- [Editor](#editor)
- [User experience](#user-experience)
- [Testing](#testing)
- [Migration](#migration)
- [Studio example](#studio-example)
- [Decisions](#decisions)

## Shared contracts

The engine and the editor implement each contract below twice, once in C++ and
once in C#. Each pair runs one shared conformance table, so the two cannot
drift.

### Reference grammar

A reference field holds one of two texts:

| Form     | Text                                                                             | Example                                       |
| -------- | -------------------------------------------------------------------------------- | --------------------------------------------- |
| Absolute | A canonical virtual path                                                         | `/Vehicles/Cars/Truck.fbx/Paint.omat`         |
| Relative | Zero or more `..` segments, or a single `.`, then one or more canonical segments | `Paint.omat`, `../Textures/Brick.otex`, `./A` |

Nothing else is a reference. `asset:///` URIs are no longer written in authored
files: scene documents store the same text as every other format. Segments are
plain text, NFC normalized, with no percent-encoding; a space is a space
(`../Sniper Rifle/Stand.fbx`).

**Anchor.** A relative reference resolves against the parent folder of the
referring file's identity: `Brick.otex` in `/Content/Materials/Wall.omat`
resolves to `/Content/Materials/Brick.otex`. The rule is the same for every
format: a buffer container names its sibling products, a model record names
its remapped materials, and an input source names an action of its own
products as `Car.input/Jump.oiact`. A file whose identity is derived from
another (a physics or scripting sidecar is named after its target scene)
anchors at its own identity, which the import request carries (see
[import jobs](#import-jobs)).

**Resolution** joins the anchor folder and the text, applies the `..`
segments, and validates the result as a canonical authored path. It fails
with:

| Code                            | When                                                                 |
| ------------------------------- | -------------------------------------------------------------------- |
| `vpath.reference_outside_mount` | The `..` segments climb above the anchor's mount root                |
| `vpath.reference_self_absolute` | Absolute text that names the anchor's own mount                      |
| `vpath.segment.dot`             | `.` or `..` after the first canonical segment, or `.` after `..`     |
| any canonical path error        | The joined path breaks a canonical path rule (length, case, Unicode) |

**Spelling** is a function of the anchor and the target:
`RelativeReference(anchor, target)` returns the shortest relative text when
both share a mount, else the target's absolute path. Every writer spells
through it, so one target has one written form per referrer.

**File references** (a texture's `source` and `sources[].file`, a buffer's
`source`, a geometry's `buffers[].uri`, a model record's `source`) follow the
same grammar against the referring file's identity, and resolve to a file
through the mount table, including `SourceMedia` and the derived folders that
asset identities refuse. `MountTable::LocateReference` is re-expressed over
`ResolveReference`, and its own code `import.reference_outside_mount` is
deleted: one refusal, one code.

| Contract        | Engine                                                               | Editor                                                            |
| --------------- | -------------------------------------------------------------------- | ----------------------------------------------------------------- |
| Resolve, spell  | `content::ResolveReference`, `content::RelativeReference`            | `AssetPath.ResolveReference`, `AssetPath.ToReference`             |
| Codes           | `VirtualPathError::kReferenceOutsideMount`, `kReferenceSelfAbsolute` | `VirtualPathError.ReferenceOutsideMount`, `ReferenceSelfAbsolute` |
| File references | `MountTable::LocateReference`                                        | `ProjectContext.LocateReference` (new)                            |
| Cases           | `Content/VirtualPathCases.json`, new `references` table              | `VirtualPathCasesTests` runs the same table                       |

The `references` table rows are `{anchor, text, resolved | code, spelled}`:
`spelled` is the text `RelativeReference(anchor, resolved)` gives back, so the
table checks resolution and spelling as one round trip.

### Input action references

An input mapping's action field holds either a local action name or a
reference. The two are told apart by the action suffix: a reference ends with
`.oiact` (`Car.input/Jump.oiact`, `/Props/Input/Shared.input/Use.oiact`), and a
local name never does, because the schema refuses an action `name` that ends
with `.oiact`. `InputActionReference.IsPath` becomes `IsReference` on that
rule, in both readers and in source analysis.

### Reference fields

The authoritative list of reference fields per format is the engine schemas,
where each reference property uses the shared `reference` definition. The
editor's `AssetReferenceField` table and `SceneReferenceFields.All` name the
same fields; a conformance test in each module compares its table against the
installed schemas' `reference` uses, so a field added to a schema without the
table fails a test.

| Format                | Reference fields                                                                                                                                               |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Material              | `textures.*.virtual_path`                                                                                                                                      |
| Geometry              | `lods/*/submeshes/*/material_ref`, `lods/*/buffers/{vb_ref,ib_ref}`, skinning buffer refs, `skeleton_ref`; `buffers[].uri` (file)                              |
| Buffer container      | `buffers[].virtual_path` (product, relative); `buffers[].source` (file)                                                                                        |
| Texture descriptor    | `source`, `sources[].file` (file)                                                                                                                              |
| Collision shape       | `material_ref`                                                                                                                                                 |
| Physics sidecar       | `shape_ref`, `material_ref`, `inner_shape_ref`, `source_mesh_ref`, `collision_mesh_ref`                                                                        |
| Scripting sidecar     | `script_virtual_path`, parameter `asset_virtual_path`                                                                                                          |
| Input source          | mapping action (see above)                                                                                                                                     |
| Scene descriptor      | `geometry_ref`, `/material` values, `scene_ref`, `context_ref`, `cubemap_ref`, `inscattering_color_cubemap_ref`, `auto_exposure_metering_mask`, `references.*` |
| Model import record   | `source` (file); `settings.material_remap` values                                                                                                              |
| Editor scene document | every field of `SceneReferenceFields.All`, script asset parameters                                                                                             |

### Identity from location

An authored file's identity is its mount path minus its authoring suffix. No
file repeats it. The model import record is the model's authored file, with
the suffix `.import.json`: `Truck.fbx.import.json` is the authored file of the
model `Truck.fbx`. The suffix belongs to model records only; no other format
may end with it.

| Removed                                                    | Where                                                                                                           |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `virtual_path` identity property                           | Texture, collision shape and physics material descriptor schemas; model record                                  |
| Identity checks                                            | `texture.descriptor.identity_conflict`, `physics.material.identity_conflict`, `physics.shape.identity_conflict` |
| Descriptor identity as an identity source                  | `TextureDescriptorImportRequestBuilder::ApplyDescriptorSettings`                                                |
| `AssetFileType.IdentityField`, `TextureSource.VirtualPath` | Editor type table and texture model                                                                             |

A geometry descriptor's `name` is not an identity: the job writes it into the
cooked geometry as its display name (`geometry_name`), so it stays. Material
and geometry descriptors have no identity field in the engine schemas; the
editor's `IdentityField` rows for them name a property those schemas do not
have.

Import manifests keep absolute job identities: a manifest is a cook
instruction for one project, not authored content.

### Mounts

`MountKind` has two values, `Source` and `Cooked`. A `Cooked` mount's binding
decides how it is read:

| Binding target                       | Read as                                                            | Who resolves it |
| ------------------------------------ | ------------------------------------------------------------------ | --------------- |
| A folder with `container.index.bin`  | A loose cooked root                                                | Engine, editor  |
| A `.pak` file                        | A pak                                                              | Engine, editor  |
| A library project's `.cooked` folder | The loose generation that `head.json` selects for the mount's root | Editor only     |

The engine never reads `.cooked`: the publication format belongs to the
editor. Wherever the editor hands the engine a mount (import manifests, the
Player launch file, the preview engine), it hands over the selected
generation's folder.

`MountState` gains `Invalid`. Every bound `Cooked` mount is inspected when the
table resolves, from a **library facts** record read without loading assets:

| Fact            | Loose root                                   | Pak                              |
| --------------- | -------------------------------------------- | -------------------------------- |
| Format version  | Index header                                 | Pak header and catalog version   |
| Roots           | Mount roots of the index's virtual paths     | The catalog's `library` name     |
| External script | Any script entry with `external_source_path` | Never (PakTool seals scripts)    |
| Requirements    | None                                         | The catalog's requirements (R10) |

| Finding                            | When                                                                     |
| ---------------------------------- | ------------------------------------------------------------------------ |
| `mount.library_root_mismatch`      | The one root differs from the mount's name                               |
| `mount.library_root_multiple`      | More than one root                                                       |
| `mount.library_external_script`    | A loose root holds a script with an external source                      |
| `mount.library_dependency_missing` | A requirement is not provided below the mount, with its type and library |
| `mount.library_format_unsupported` | A version this build cannot read, or a file that is not that format      |

`mount.library_unindexed` from the proposal is not needed: every pak has a
browse index (see [paks](#paks)).

A mount with a finding is `Invalid`: it stays in the table and in Content
Explorer with its finding, contributes no assets, and is never added to a
cook's, a run's or a preview's cooked roots. Findings are warnings for opening
a project, like `mount.folder_missing`.

Further mount rules this design fixes:

- **One mount-name rule.** `IsValidMountName` and the editor's
  `MountTableBuilder` call the virtual path mount rule
  (`content::ValidateMountName`, `VirtualPath.ValidateMount`) and add only the
  table's own restrictions: no leading `.` and no reserved name. The schema
  pattern in `oxygen.mounts.schema.json` and `Project.oxy` follows it. The
  64-character cap that only the mount table had is gone.
- **Cooked folders overlap like source folders.** A `Cooked` mount bound to a
  folder inside a `Source` mount, or the reverse, is `mount.folder_overlap`.
  Today only `Source` pairs are compared.
- **Own and shared roles.** `Mount::role` (engine) and `ResolvedMount.Role`
  (editor) are `Own` when the canonical folder lies inside the project root,
  else `Shared`, as R1 states. A `Cooked` mount is always `Shared`.

The shared case table `Cooker/test/Import/MountTableCases.json` keeps its
layout fixtures and gains **library fixture recipes**: a case names a loose
root or pak by its facts (`{ "library": { "kind": "pak", "root": "Props",
"format": "current", "requirements": [...] } }`), and each language's harness
builds the binary from the recipe with its own fixture writer
(`LooseCookedTestWriter`, `PakTestWriter`; `LooseCookedIndexFixture`,
`PakFixture`). The `.cooked` binding cases are editor-only and live in the
editor's mount tests.

### Paks

The pak catalog format goes to version 4:

| Field          | Version 3                                       | Version 4                                                        |
| -------------- | ----------------------------------------------- | ---------------------------------------------------------------- |
| `library`      | absent                                          | The pak's one root name; a game's own pak names its own mount    |
| `bases`        | `{source_key, content_version, catalog_digest}` | Unchanged                                                        |
| `dependencies` | `{source_key, content_version, catalog_digest}` | `{library, requirements[]}` with requirement `{kind, key, type}` |
| `entries`      | Unchanged                                       | Unchanged                                                        |

A requirement's `kind` is the `KeyReferenceKind` the planner resolved: an
asset (`AssetKey` and `AssetType`), a texture (texture key) or a physics
resource (resource key and type). Logical references are internal to a pak
and are not requirements.

Every pak embeds its browse index; PakTool's `--embed-browse-index` option is
deleted and the browse index is always written. The `library` name is checked
at build: every planned entry's virtual path lies under it, else
`pak.plan.root_multiple`. A deletion-only patch names the library of its base.

**Checking requirements.** `ContentSourceRegistry::ValidateMount` replaces
`PakCatalog::ValidateDependencies`: for each pak at or above the changed layer,
each requirement is resolved over the layers below that pak with
`ResolveAssetKeyByPrecedence` (tombstones terminal), and passes when the
providing layer exists, has the required type, and lies under the required
`library`. Loose layers can satisfy a requirement; their library is the root of
the providing entry's virtual path. Patch bases keep their exact identity check
(`ValidateBaseLayers`). The refusal is a typed `DependencyRequirementError`
listing each failed requirement, not a `std::invalid_argument` string.

**Packing a project** requires every library it depends on to be a pak:

- The editor's pak build checks the resolved mount table and refuses with
  `pak.dependency_not_pak`, naming each library mount bound to a loose root or
  `.cooked`, before PakTool runs.
- PakTool reads dependencies only from `--dependency-pak`. A reference that no
  dependency pak resolves stays `pak.plan.asset_reference_missing`, and its
  message names the library from the reference's root. PakTool has no input
  that could tell a loose library from a missing one, and needs none.

The Player catches a requirement refusal at launch and reports
`player.launch.dependency_missing` with the pak and the failed requirements,
instead of the generic exception handler.

### Model import records

A record lives at `<model identity>.import.json` in the model's mount. It
holds settings, `source_identity`, surface provenance, node continuity,
published nodes and the fingerprint; it holds no `virtual_path`. Its `source`
is a file reference: a companion (`Truck.fbx` beside it, or a file in the
folder that holds that companion), a file elsewhere in the mount
(`../SourceMedia/DCC/Lantern.glb`) or a file in another mount
(`/RawMedia/Cars/Truck.fbx`).

**Finding records.** A record is found at its identity; nothing scans a
records folder. The few operations that need every record (source relocation,
`source_identity` uniqueness, Content Explorer rows) enumerate `*.import.json`
in the bound `Source` mounts, own and shared, through the existing mount
enumeration (`FileSystemAssetCatalog` in the editor, the mount table in the
engine). A source has any number of records; "the record of a source" is no
longer a question the code can ask.

**Manifest model jobs** name the model identity in `virtual_path` and nothing
else: the tool reads the record at that identity through the declared mounts.
The `record` field is removed.

## Engine

### Content

| Type                                | Change                                                                                                              |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `VirtualPath.h/.cpp`                | `ResolveReference`, `RelativeReference`, the two errors and their `to_string`                                       |
| `VirtualPathCases.json`             | `references` table; `TableCoversEveryRefusalCode` includes it                                                       |
| `Docs/virtual_paths.md`             | References section; the dot rule is stated for canonical paths only                                                 |
| `IContentSource`                    | `FindVirtualPath(key)` and `GetLibrary()`; pak from the catalog, loose from the index                               |
| `ContentSourceRegistry`             | Requirement check in `ValidateMount`; `lower_layers` identity list kept only for bases                              |
| `PakFile`                           | Typed open failure (`PakOpenError::{kNotAPak, kUnsupportedVersion, kCorrupt}`) instead of `std::runtime_error` text |
| `LooseCookedIndex`                  | Typed open failure with the same reasons                                                                            |
| `Docs/patch_resolution_contract.md` | Dependency requirements next to base compatibility                                                                  |
| `Docs/loose_cooked_content.md`      | Records are mount files; the `Imports/` paragraph is removed                                                        |

### Data

`PakCatalog` keeps `PakCatalogBase` for `bases` and gains
`PakCatalogDependency { std::string library; std::vector<PakRequirement> }`
with `PakRequirement { KeyReferenceKind kind; uint128 key; uint16 type }`.
Records are variable length; `EncodeContents` and `Decode` write and read them;
`ValidateStructure` checks that a library appears once and is not the pak's
own. `kPakCatalogVersion` becomes 4. `PatchCompatibilityEnvelope` keeps
`required_bases` and replaces `required_dependencies` with the dependency
records. `binary_packing_discipline.md` describes the new records.

### Import jobs

One resolver replaces the five that exist today
(`ResolveMountedAssetReference`, `ResolveTextureReference`, geometry's three
local resolvers, `VirtualPathResolver::ResolveAssetKey` in the sidecars and
collision shapes, and input's local `Resolve`):

```cpp
struct ResolvedReference {
  std::string written;   // text as authored
  std::string resolved;  // canonical virtual path
};
auto ResolveJobReference(const ImportRequest& request, std::string_view text)
  -> Result<ResolvedReference, ReferenceRefusal>;
auto LookupReference(MountedCookedSources&, const ImportRequest&,
  const ResolvedReference&, AssetType expected) -> Result<AssetKey, ReferenceRefusal>;
```

`ResolveJobReference` lives in `Internal/Utils/VirtualPathResolution.h`, next
to `PlaceAsset`, and anchors at `request.AnchorIdentity()`. `AnchorIdentity` is
`asset_virtual_path` for every job that has one, and the sidecar's own identity
for physics and scripting sidecars (`target_scene_virtual_path` with the
sidecar suffix), which the request builders set. Every refusal carries the
written text, the resolved path and the file looked up, so a diagnostic says
where it looked. The per-job codes for an invalid or unmounted reference
(`*_virtual_path_invalid`, `*_unmounted`, `material_ref_invalid` and the rest)
collapse into `import.reference_invalid` and `import.reference_unmounted`,
with the field's JSON pointer as the object path; `reference_missing` and
`reference_type_mismatch` stay per family because they carry family facts.

| Job or reader                                   | Change                                                                                                 |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `MaterialSource`, `MaterialDescriptorImportJob` | Texture bindings resolved on read                                                                      |
| `SceneSource`, `SceneDescriptorImportJob`       | Every reference resolved on read; `index_cache` keyed by resolved path                                 |
| `GeometrySource`, `GeometryDescriptorImportJob` | The three local resolvers deleted; buffers, skeleton and material through the one resolver             |
| `BufferSource`, `BufferImportSubmitter`         | Products resolved against the container; the product's mount is the container's                        |
| `CollisionShapeDescriptorImportJob`             | Identity check deleted; `material_ref` through the resolver                                            |
| `PhysicsMaterialDescriptorImportJob`            | Identity check deleted                                                                                 |
| `PhysicsSidecarImportPipeline`                  | All refs through the resolver, anchored at the sidecar identity                                        |
| `ScriptingSidecarImportPipeline`                | `script_virtual_path` and asset parameters through the resolver; rootless text is a relative reference |
| `InputImportPipeline`                           | `.oiact` rule; references through the resolver                                                         |
| `ModelSettings::ResolveMaterialRemap`           | Through the resolver, anchored at the model identity                                                   |
| `TextureDescriptorImportRequestBuilder`         | Identity from the job only; `identity_conflict` deleted; every producer passes the identity            |
| `ImportSourceAnalysis`                          | Dependencies report `written` and `resolved`; local buffers compared resolved                          |
| `EngineContent`                                 | The built-in geometry names its default material relatively                                            |

Schemas: `oxygen.reference.schema.json` defines `reference` (the grammar as a
pattern) and `file_reference`; it is registered in `Cooker/CMakeLists.txt` and
installed with the other schemas. The 20 `canonical_*_path` definitions in
reference positions are deleted; identity patterns in the asset key map and key
request schemas stay. The input schema's `action_reference` becomes
`anyOf[local name not ending with .oiact, reference ending with .oiact]`.
`oxygen.source-analysis.schema.json` adds `written`.

### Model import records

`ModelImportRecord` keeps reading, writing and the generation store, and loses
every `Imports/` concept:

| Today                                                                              | After                                                                                                |
| ---------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `kRecordsFolder`, `RecordRelativePath`, `PathFor`, `ContentRootOf`, `RootOfRecord` | Deleted. `RecordPathOf(MountTable, identity)` and `IdentityOf(MountTable, record path)`              |
| `content.virtual_path`                                                             | Deleted; the identity is passed in or derived from the path                                          |
| `LocateRecordSource` (`MountPathOf` of folder + text)                              | `MountTable::LocateReference`                                                                        |
| `RecordPaths`, `ScanRecords` over `<root>/Imports`                                 | `ScanRecords(MountTable)` over bound `Source` mounts                                                 |
| `RequireUniqueRecord` (virtual path and `source_identity`)                         | `source_identity` uniqueness only, over the scan                                                     |
| `LocateRecord` 4-step lookup keyed on `virtual_path`                               | Same steps keyed on the identity: at the identity, source has a model, relocated by fingerprint, new |
| `Save` moves records under `Imports`, prunes empty folders                         | `Save` writes at the identity; moving a record is a relocation, not a save                           |
| `ModelImportRecordQuery/Save.content_root`                                         | `MountTable` (the content root is no longer an input)                                                |

The generations folder that DemoShell borrows `kRecordsFolder` for gets its own
constant, `kModelGenerationsFolder`.

ImportTool: `--record <path>` is the model's authored file; `--root` and
`--virtual-path` locate it through the mounts (`--mount` or `--mounts-file`).
`ReadModelRecord` and `MakeModelJob` in `ImportManifest.cpp` read the record at
the job identity. The README's record section is rewritten.

### Source mounts

`SourceMounts.h/.cpp` and `ImportManifest.cpp`:

- `MountKind::kPak` deleted; `ReadMountKind` refuses an unknown kind with
  `mount.kind_unknown` instead of silently making it `Source`.
- `mount.path_kind`: only a `Source` mount bound to a file.
- `MountState::kInvalid`, `Mount::findings`, `Mount::role`.
- `ResolveMountTable` reads library facts for bound `Cooked` mounts through a
  `LibraryFacts ReadLibraryFacts(path)` function in `Cooker/Loose/Inspection`
  (loose root or pak, no asset loading) and applies the checks above. The
  dependency check needs the layers below, so it runs once the table is
  ordered: `Cooked` mounts are checked in priority order against the resolved
  view of the mounts below them.
- `MountedCookedSources` takes `Cooked` mounts from the table instead of only
  `cooked_context_roots`, and handles paks, which its comment already claims.
- PakTool's `ResolveSourceMountFolders` excludes `Invalid` mounts.

### PakTool and Pak

| Type                                                      | Change                                                                                              |
| --------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| `PakBuildRequest`                                         | `dependency_catalogs` become `dependencies { library, catalog, browse index }`                      |
| `RequestPreparation::ReadLayerCatalogs`                   | Reads `library` and the browse index of each `--dependency-pak`                                     |
| `PakPlanBuilder::CollectDependencies`                     | Per-dependency key and texture maps instead of one flat map                                         |
| `PakPlanBuilder::ValidateAssetReferences`, texture checks | Record each used key with its kind, type and providing library                                      |
| `PakPlanBuilder::BuildOutputCatalog`                      | Writes `library` and the dependency records                                                         |
| `CliBuilder`                                              | `--embed-browse-index` deleted; `--library` is not needed: the library is the planned entries' root |
| `PakCatalogIo`, `oxygen.pak-catalog.schema.json`          | Sidecar JSON `schema_version` 4; `layer` split into `base` and `dependency`                         |
| `PakManifestWriter`                                       | Writes dependency records                                                                           |
| `PakFileDumper`                                           | Prints the library and requirements                                                                 |
| `README.md`, `paktool_design.md`                          | Lower layers, options and dependencies rewritten                                                    |

`engine.pak` (`EngineContent/CMakeLists.txt`) and
`Examples/RenderScene/Tools/pak.ps1` drop `--embed-browse-index` and are
rebuilt.

### Player

`PlayerModule::OnHostAttached` catches `DependencyRequirementError` and reports
`player.launch.dependency_missing`. The launch file shape (version 3) is
unchanged: the editor passes generation folders and pak files.

### Examples and tools

The Showcase mount is renamed `Showcase`; every hard-coded `Content` mount name
in the examples follows it: `DemoMain.cpp`, `VortexBasic/main_impl.cpp`,
`DemoShell` (`ContentVm`, `FileBrowserService`), `RenderScene/Tools/{cook,play,pak}.ps1`,
`internal/ExamplesTools.ps1`, `internal/prepare_showcase.py`, `RuntimePaths.cpp`,
`Showcase.cmake`, `Generators/generate_city.py`, `Examples.json`, the Scripting
and Cooker test CMake lists that consume Showcase, and `tools/cmake/tests/test_showcase.py`.

`cook.ps1 -Check` re-implements the reference and identity rules in
PowerShell; that check is deleted, and `-Check` runs ImportTool's source
analysis instead, so the rules have one implementation.

DemoShell's scratch project gets its own writable `Source` mount `Scratch` in
the scratch folder; scratch imports are records in that mount
(`/Scratch/<name>/<file>.import.json`), never in the read-only examples mount.

## Editor

### Oxygen.Managed.Core

- `AssetPath.ResolveReference(anchor, text)` and `AssetPath.ToReference(anchor)`
  over `VirtualPath.ValidateSegment` for the canonical tail; the two
  `VirtualPathError` members with codes and messages.
- `AssetPathJsonConverter` stays the converter of absolute identities (settings,
  templates, manifests). Authored reference fields do not use it (see scene
  documents).
- `README.md`: References section; the stale `AssetUris` section is removed.

### Oxygen.Managed.Assets

| Type                                                                 | Change                                                                                                                                                                                                                |
| -------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `AssetFileType.IdentityField`                                        | Deleted                                                                                                                                                                                                               |
| `AssetFileTypes`                                                     | `.import.json` maps to the model identity (`ToIdentity("Truck.fbx.import.json") == "Truck.fbx"`); `SourcesOf(model)` returns the record; the companion rule (R5) is a type-table member, `CompanionsOf(file, source)` |
| `MaterialSourceReader/Writer`                                        | `Read(bytes, identity)`, `ToJson(material, identity)`; resolve and spell through `AssetPath`                                                                                                                          |
| `TextureSource`, reader, writer                                      | `VirtualPath` and `VirtualPathInvalid` deleted; images resolved through a `IFileReferenceLocator` port instead of `Path.IsPathRooted`                                                                                 |
| `InputActionReference`, input reader/writer                          | `.oiact` rule; identity parameter                                                                                                                                                                                     |
| `PakIndex`, `PakDocument`                                            | Catalog version 4: `Library`, `Dependencies`; typed format failure                                                                                                                                                    |
| `LooseCookedIndex`                                                   | `HasExternalScripts`, `Roots`                                                                                                                                                                                         |
| `LibraryFacts`                                                       | New: the managed twin of the engine's library facts, read from either reader                                                                                                                                          |
| `Publication/CookPublicationHead`, `CookPublicationDocument` readers | Moved here from ContentPipeline as read-only types, so Projects can resolve a `.cooked` binding; ContentPipeline keeps writing them                                                                                   |

`IFileReferenceLocator` is the port the asset readers need to turn a file
reference into a file; `ProjectContext` implements it. The readers stay free of
the project model.

### Oxygen.Editor.Projects

| Type                                               | Change                                                                                                                                                                                                  |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MountKind`                                        | `Pak` deleted                                                                                                                                                                                           |
| `MountState`                                       | `Invalid`                                                                                                                                                                                               |
| `MountCodes`                                       | The five `mount.library_*` codes, `mount.kind_unknown`; `IsWarning` covers them                                                                                                                         |
| `ResolvedMount`                                    | `Role`, `Findings`, `Library` (facts), `Container` (`LooseRoot`, `Generation`, `Pak`)                                                                                                                   |
| `MountTableBuilder`                                | Binding target decides the container; library facts and checks; overlap of cooked folders; one mount-name rule                                                                                          |
| `ProjectMounts.Resolve`                            | Resolves `.cooked` bindings to the generation `head.json` selects for the mount's root                                                                                                                  |
| `ProjectContext`                                   | `LibraryMounts` = bound, valid `Cooked`; `LocateReference(referrer, text)` implements `IFileReferenceLocator` for every bound mount                                                                     |
| `ProjectAssetFiles`                                | `Find` over a pak or generation goes through the library facts, not `Root.Combine`; `SourceMountPathOf` and `LocateSourceMountPath` replaced by `LocateReference` and `ToFileReference(referrer, file)` |
| `CookedContentSourceKind`, `CookedContentOrdering` | `Pak` deleted; the order lists `ProjectOutput` and library mounts by name; the container comes from the binding                                                                                         |
| `ProjectCreationService`                           | A template may declare a library mount instead of `Content`: `Template.json` `Role: Library` names the mount after the project; a library template ships with the editor                                |
| `ProjectInfo.CurrentSchemaVersion`                 | 3: version 2 files fail with `UnsupportedProjectSchemaException`, as version 1 does; the migration writes version 3                                                                                     |

### Oxygen.Editor.Models

`ModelImportSettings` loses `RecordsFolder`, `PathFor`, `RelativePathFor`,
`ProjectRootOf` and `VirtualPath`. `Parse(bytes, identity)` and
`ToBytes()`; `Source` is a file reference, `ResolveSource(project)` goes
through `LocateReference`. `ModelImportRecords` is built from the `Source`
mount enumeration and indexes records by identity; `TryFindBySource` is
replaced by `TryFind(AssetPath identity)` and `RecordsNaming(source file)`,
which returns every record of a source.

The 28 production call sites of `TryFindBySource` and `ModelRecordLookup` move
to identity lookup. The ones that start from a file (a model document opened on
a record, a Content Explorer row) already hold the record path, so its identity
is `AssetFileTypes.ToIdentity` of the path.

### Oxygen.Editor.ContentPipeline

**Cook inputs.** A model's cook input is its record; `CookInputResolver`,
`HasProjectSource`, `CookRootRepair.IsRetired`, `CookPaths.ExistsExactly`,
`ImportedOutputs` and `ContentCookAssetKinds` find the authored file through
`AssetFileTypes.SourcesOf`, which returns the record for a model. The
project-relative cook path branch for records (`Imports/...`) is deleted;
`CookPaths` keeps it only for generated files under `.build`.

**Manifests.** `ContentImportManifestBuilder` writes model jobs with
`virtual_path` only; `ContentImportJob.Record` and its validator rule are
deleted. Manifests declare `Cooked` mounts with the resolved generation folder
or pak.

**Generated scene descriptors.** `SceneDescriptorGenerator` spells every
reference through `RelativeReference` against the descriptor's job identity,
so the engine applies `vpath.reference_self_absolute` without exceptions.

**Analysis diagnostics.** `CookSourceAnalyzer` resolves material texture
references against the material identity and reports the written text, the
resolved path and the file it looked up. A texture in a `Cooked` mount is
checked against that library's facts instead of being skipped.

**Reference check.** `ReferenceCheck.UnresolvedCode` resolves references into
`Cooked` mounts against the library's assets, so an asset removed from a
library is unresolved (`reference.unresolved`); it adds
`reference.into_project_mount` and the R2 refusals. `.build/references.json`
version 2 entries are `{file, property, written, resolved, code}`.
`UnresolvedReference` carries the written text, so Retarget can rewrite it.

**Relocation** (R8):

| Area                         | Change                                                                                                                                                                                                     |
| ---------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `AuthoredReferenceFormat`    | Reads references with the file's identity; rewrites by resolving, mapping and respelling with the file's new identity; no identity field                                                                   |
| `AuthoredReferenceIndex`     | Records are descriptors of their mount; `ImportSource` is the record plus its companions                                                                                                                   |
| `AssetRelocationPlanner`     | Companions follow (generalised from `AddTextureCompanions`); a move into a `Shared` mount adds the R3 closure; references into an own mount from shared content refuse with `reference.into_project_mount` |
| `AssetRelocationService`     | Copy and duplicate of models (new `source_identity`); delete recycles only companions no other asset names, never a source in another mount                                                                |
| `AssetRelocationTransaction` | `PruneEmptyRecordFolders` deleted                                                                                                                                                                          |
| `RelocationPaths`            | Writable mounts are bound `Source` mounts with their role; review texts name shared mounts (R9)                                                                                                            |
| `AssetRedirects`             | Kept: it replays this session's committed relocations to open documents, maps identities only, and is not the library redirect table R9 rejects                                                            |

**Asset packages** (R11): `AssetPackageExport`, `AssetPackageImport` and
`ReplaceWithLibrary` in `ContentPipeline/src/Packages/`, each planned with the
relocation planner's closure and committed through `AssetRelocationTransaction`
for Undo. The package root is the deepest folder holding the selection and its
closure; destinations keep every path below it. Export copies sources in other
mounts in as companions and respells them; Import package respells references
into its own mount relative. `oxygen.asset-package.schema.json` lives in
`Oxygen.Editor.Schemas` with the editor's other formats.

**Pak build.** `PakBuildService` runs PakTool through
`EngineContentPipelineToolLocator`, after the R10 check of the mount table
(`pak.dependency_not_pak`). It packs the project's published cooked root with
`--dependency-pak` for each library pak, lowest first, and `engine.pak`.

### Oxygen.Editor.World and Oxygen.Editor.Scenes

Scene documents are read and written in one place: `SceneFiles` in Scenes
owns `SceneFileJsonContext`, and `SceneSerializer`, `SceneDocumentKind`,
`SceneContent` and `ModelPreviewFiles` call it. Reading takes the scene's
identity and resolves every field of `SceneReferenceFields.All` and script
asset parameters into canonical `AssetPath`s before the typed model is built;
writing spells them back with `ToReference`. The in-memory scene keeps
canonical absolute paths, so nothing past the serializer changes.
`AssetPathData.Require` and the `SceneInstanceComponent.Source` docs follow.

### Assets and Content Explorer

| Area                                        | Change                                                                                                                                                         |
| ------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `AssetFeed`, `AssetEntry`                   | A model row is the record's identity; the record and companions are never rows; raw sources in `Source` mounts with no record naming them are rows with Import |
| `AssetBrowserQuery`                         | "Not imported" means a raw source row                                                                                                                          |
| `AssetCatalog`                              | Library catalogs come from library facts for every valid `Cooked` mount, pak or loose                                                                          |
| `ContentMountKind`                          | `LocalLibrary` and `PakLibrary` merge into `Library`; `Invalid` presentation                                                                                   |
| `CookedLibraryFolders`, pak tree            | Library assets are shown at their own virtual paths; nothing re-roots them under a typed name                                                                  |
| `ContentMountsViewModel`, `ContentMountRow` | Add library replaces the typed-name add of libraries; libraries cannot be renamed                                                                              |
| `UnavailableMountTreeItemAdapter`           | A third state, `Invalid`, with the finding's message                                                                                                           |
| `AssetsViewModel`                           | Import from a raw source row; destination limited to own mounts; copy, duplicate and delete of models (E3 to E5)                                               |
| `TextureImport`, `TextureImportTarget`      | A source in a mount is referenced, not copied; the descriptor has no `virtual_path`                                                                            |

### Problems

`ProblemsViewModel` adds Retarget on `UnresolvedReferenceProblem`: the asset
picker filtered to the field's asset type, then one `RelocationEdit` that
rewrites the written text with `ToReference`, committed as an Undo step.

### Play and Live Preview

`PlaySession` writes the generation folder selected at launch for each
`.cooked` binding. `PreviewSession` watches the `head.json` of each `.cooked`
binding next to `.mounts.json`; on a change it mounts the new generation with
`MountLooseCookedGeneration(new, replaces: old)`. If the engine refuses the
swap with a requirement error, the old generation stays and the mount reports
`mount.library_dependency_missing`; otherwise the reference check runs again.

## User experience

The new commands reuse the editor's existing dialogs and controls; the plan's
UX slice produces SVG mockups in the editor's style before any of them is
built.

| Command              | Where                                                      | Flow                                                                                                                                                                                                                        |
| -------------------- | ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Add library          | Content Explorer, mounts dialog                            | Pick a `.pak`, a cooked folder or a library project's `.cooked`; the dialog shows the library name read from it, its dependencies and where it will be placed; Add writes the committed path or the placeholder and binding |
| Export package       | Content Explorer, on assets or folders of a writable mount | Review lists every asset with its destination under the chosen library folder, the sources copied in and the dependencies; Export writes the `.zip`                                                                         |
| Import package       | Content Explorer of a library project                      | Pick the `.zip`; review lists every destination; refused with the reasons when any destination exists                                                                                                                       |
| Replace with library | Content Explorer of the game                               | Pick the `.zip`; review shows the three lists of R11; Replace commits one Undo step                                                                                                                                         |
| Retarget             | Problems, on an unresolved reference                       | Asset picker filtered to the field's type                                                                                                                                                                                   |
| Build pak            | Project menu                                               | Refused with the list of loose library mounts, or runs PakTool and shows its report                                                                                                                                         |

## Testing

The plan applies three levels; each slice writes unit and functional tests
only, and the existing integration tests are fixed together in the last slice.

| Level       | Definition                                                                                                            |
| ----------- | --------------------------------------------------------------------------------------------------------------------- |
| Unit        | One type or function, in memory: `MockFileSystem`, fakes, byte buffers; no engine, process, cook, window or temp disk |
| Functional  | One module's public surface through its ports with fakes; one scenario per test                                       |
| Integration | Native engine, real cook or pak, real disk, interop, WinUI lanes                                                      |

Most engine cooker tests named `*_functional_test` write a temp folder and run
the real import service; they are integration by this definition. New engine
tests for this work are unit tests over pure functions and in-memory types:
reference resolution, schema validation, catalog encode and decode, requirement
checks over fake `IContentSource` layers, library facts over in-memory index
and pak bytes, and mount table resolution over an in-memory file system port.

**Single writers.** Every authored format gets one test writer per language,
which the tests use instead of inline JSON:

| Format                                                                                                                                    | Engine (`Cooker/test/Support/AuthoredFiles.h`)                                                | Editor                                                                                                                 |
| ----------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| Material, texture, geometry, buffer container, collision shape, physics material, physics and scripting sidecars, input, scene descriptor | One builder per format, emitting current-contract JSON                                        | `Oxygen.Managed.Assets.Testing` `AuthoredFiles` over the production writers                                            |
| Model record                                                                                                                              | `ModelRecordBuilder`                                                                          | `Oxygen.Editor.Models.Testing` (new kit) `ModelRecords`                                                                |
| Project, mounts, `.mounts.json`                                                                                                           | `TestPaths` gains cooked and library mounts                                                   | `Oxygen.Editor.Projects.Testing` `TestProject` gains library mounts; the local `CreateManifestJson` copies are deleted |
| Loose root, pak                                                                                                                           | `LooseCookedTestWriter`, `PakTestWriter` with entries, browse index, library and requirements | `LooseCookedIndexFixture`, `PakFixture` with the same                                                                  |
| Editor scene document                                                                                                                     | n/a                                                                                           | `Oxygen.Editor.Scenes` `testing/TestSceneDocuments` writes through `SceneFiles`                                        |

A builder writes the contract's current form, so a later format change edits
one builder per language. The three editor record writers
(`RelocationWorkspace.WriteModel`, `RetainedModelScenario`,
`ProducedSourceMetadataTests`) and the 16 hand-rolled record sites move to the
Models kit.

**Shared tables.** `VirtualPathCases.json` (references), `MountTableCases.json`
(library recipes) and a new `ModelImportRecordCases.json` (record path to
identity, `source` forms and their resolution) are each run by the engine and
the editor.

## Migration

A one-time Python script, `tooling/migrations/portable_references/`, kept
outside the product, typed and checked to the repository's Python bar. It
rewrites data only; no product code reads an old form. It runs per project,
libraries first, and writes a report of every file it rewrote and every
reference it could not resolve.

1. Resolve the project's mount table as its author meant it.
2. Respell every authored reference by R2, scene documents included
   (`asset:///` URIs become reference text).
3. Remove identity `virtual_path` fields; make buffer products relative.
4. Move each `Imports/<Mount>/<path>.import.json` to `<mount folder>/<path>.import.json`,
   remove `virtual_path`, respell `source`; delete `Imports/` and the derived
   copies under `.build/cook/*/inputs/Imports/`.
5. Rewrite `Project.oxy` to schema version 3; `Pak` mounts become `Cooked`.
6. Rewrite import manifests: drop model job `record`, rename mounts.

Cases the script handles by rule, from the data:

| Case                                                                                  | Rule                                                                                                                                                                                                                 |
| ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| An old editor sidecar (`SchemaVersion` field) already at a new record path (5 files)  | Deleted before the move: it is a retired format, not a record                                                                                                                                                        |
| Vortex `*.omat.json.import.json` legacy material sidecars (3)                         | Deleted: retired format                                                                                                                                                                                              |
| Instancing's record of `/BurgerPiz/Models/BurgerPiz.glb`, whose mount is a raw folder | The model is re-identified in Instancing's own mount (`/Content/BurgerPiz/BurgerPiz.glb`), its `source` names `/BurgerPiz/Models/BurgerPiz.glb`, and the one scene reference is rewritten; `source_identity` is kept |
| F1 Validation's two `asset:///.cooked/...` references                                 | Reported unresolved; fixed by hand to the authored script and input context                                                                                                                                          |
| Showcase sources under `SourceMedia/DCC`                                              | Become same-mount file references (`../SourceMedia/DCC/Lantern.glb`); the files stay                                                                                                                                 |

Showcase becomes a library project with mount `Showcase`; its 21 manifests,
11 records and the examples tooling follow the rename in the same change.
BurgerPiz stays a game: nothing mounts it.

## Studio example

`Examples/Studio` is a small editor workspace that exercises the scenarios
no existing project covers:

| Project              | Kind                              | Shows                                                                                                                          |
| -------------------- | --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `Libraries/Vehicles` | Library project, mount `Vehicles` | A model with a companion source, a material, an input source and a script                                                      |
| `Libraries/Props`    | Library project, mount `Props`    | Assets that reference `Vehicles` (L10)                                                                                         |
| `Games/Racer`        | Game                              | `Vehicles` as `Cooked` from its `.cooked`, `Props` as a pak, a `RawMedia`-style shared source mount with a per-game model (V1) |

Its README lists the steps that check L4, L5, L9, L10, S9, P4 to P7 and V1 by
hand in the editor; the integration slice automates the ones that run without a
window.

## Decisions

- **One anchor for every format: the referrer's parent folder.** Containers
  name their products like any other reference; no format needs its own rule.
- **Authored files store reference text, never `asset:///` URIs.** One grammar
  in every file; the URI form stays an in-memory and settings spelling of an
  absolute identity.
- **Input references end with `.oiact`.** It is the smallest rule that keeps
  local action names and makes relative references unambiguous.
- **One resolver per language.** Five engine resolvers and two refusal codes
  for the same rule were the debt that let the F1 failure say nothing about
  where it looked.
- **The engine never reads `.cooked`.** The publication format is the editor's;
  the engine receives generation folders, as it does today.
- **Every pak has a browse index and names its library.** A requirement needs
  the providing asset's root, and an optional index made that fact optional.
- **Requirements cover assets, textures and physics resources.** The planner
  already resolves all three across layers; recording only assets would leave
  a removed library texture unchecked.
- **PakTool keeps one dependency input.** The editor names loose libraries
  before PakTool runs; PakTool cannot see them and needs no new option.
- **Records are found at their identity.** The few whole-project queries
  enumerate the mounts; no index folder exists to fall out of date.
- **Project schema version 3.** The `Pak` kind and records outside mounts are
  refused as version 2, like version 1 today, with no reader for either.
- **Mockups before the new commands are built.** Add library, packages,
  Retarget and Build pak are new UI; the owner decides them on SVG mockups.
