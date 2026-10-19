# Asset identity: debt elimination

Status: **complete**. Every slice has landed. The contract this plan enforced is
the [asset identity LLD](../lld/asset-identity.md); the item IDs below name the
entries of the debt register the plan worked from, which the LLD replaced. The
one open follow-up is [#81](https://github.com/abdes/DroidNet/issues/81).

Last Updated: 2026-10-20 01:32

Summary:

- [1. Rules for every slice](#1-rules-for-every-slice)
- [2. Slices](#2-slices)
- [3. Coverage](#3-coverage)

## 1. Rules for every slice

- A slice leaves the tree working: the engine, the editor and the examples agree
  on the format and layout at every commit. A slice that changes what the cooker
  emits changes the editor code that reads it in the same commit.
- No slice brings back a clash of section 3.3. A mechanism that fixes a clash is
  removed only in the slice that lands the rule keeping it fixed, and that slice
  rewrites the pinning tests to the rule.
- A cooked format or layout change bumps its version; content is recooked, never
  migrated (Oxygen rules). No cooked bytes are tracked in the repository; test
  fixtures are written by their writers.
- An engine slice builds and runs its engine targets' tests under `vcvars64`. An
  editor slice after it needs that SDK installed at
  `projects/Oxygen.Engine/out/install/Debug`. Editor slices build their owning
  test projects only.
- Tests assert refusal and diagnostic codes, never messages.
- Each slice marks its register items closed in `asset-identity.md`. The last
  slice removes the register.
- The plan runs before issue #69 S1.8 resumes: the Input module of
  [asset-family-modules.md](../lld/asset-family-modules.md) is built on slices
  12 and 13.

## 2. Slices

### Phase 1: names and paths

**Slice 1. Engine virtual path rules.** VP-01, VP-02, VP-11, VP-10 (engine),
VP-12 (rule), VP-05 (rule), VP-08 (engine documentation).

- `VirtualPath.h` implements 1.7: segment, leaf, derived content, case folding,
  length and mount name rules. Every refusal has a stable code
  (`vpath.segment.reserved_character`, `vpath.segment.too_long`, ...);
  `ValidateCanonicalVirtualPath` returns the code. Two rule sets: a cooked path,
  and an authored path, which adds the derived content rule.
- The 512-byte path bound, the ASCII character list and the dot rules go. The
  `.cooked` special case becomes the reserved mount rule.
- `Content/Tools/UnicodeCodeGen` generates
  `Content/Internal/Generated.UnicodeTables.h` (assigned ranges, simple case
  folding) from the pinned UCD in `Content/Unicode/15.1.0`, as a build step of
  Content that runs when the data or the tool changes; utf8proc provides NFC.
- `Content/VirtualPathCases.json`, beside the header, lists accepted and refused
  inputs with their codes. `VirtualPath_test.cpp` runs it; tests that asserted
  the old refusals change to the table.
- The pak planner checks mount names through the same function
  (`PakValidation.cpp:121`).
- `Content/Docs` owns the rules; `Data/Docs/physics.md` and
  `loose_cooked_content.md` link to them.
- Gate: Content, Cooker and Pak tests that cover virtual paths.

**Slice 2. Managed.Core virtual path rules.** VP-08 (editor), VP-10 (editor),
VP-12 (editor), VP-05 (editor), VP-01 and VP-02 (editor side).

- `Oxygen.Managed.Assets/src/Filesystem/VirtualPath.cs` moves to
  `Oxygen.Managed.Core` and implements 1.7 with the same codes. A Managed.Core
  source generator builds its tables from the same UCD files; the editor build
  needs neither Python nor an engine build.
- The Managed.Core tests run `VirtualPathCases.json`, linked from the engine
  tree as the contract's artifact.
- The editor's validators go: `RelocationPaths.ValidateName`, the importers'
  target checks, `ContentPipelinePaths.IsCanonicalVirtualPath`,
  `ContentMountsViewModel.ValidateName`. `GetVirtualMountRoot` checks the mount
  rule. Every caller uses the Managed.Core rules.
- The import targets' excluded folder lists (`SceneImportTarget`,
  `TextureSourceAssetImporter`) use the derived content rule. The pipeline's
  output folders are dot folders, a different notion: `SourceMedia` holds
  sources, which the folder scans and the mount checks must see.
  `ProjectOutputFolders` moves to Projects as the one definition that project
  validation and the pipeline share.
- Mount settings apply the authored mount rule to project and derived mounts; a
  local library's name is a label, since its virtual paths come from its own
  cooked index.
- Mount names, relocation containment and the authoring checks compare ordinal
  (`FindWritableMount`, `IsSameOrInside`, every mount name equality). A
  case-only sibling is refused through `VirtualPathComparer.CaseFolded`
  (`AssetAuthoring`, mount settings, project mounts). Typed names are put in
  NFC. The remaining case-insensitive path and extension comparisons go with the
  identity type (slice 6a) and `AssetFileTypes` (slice 8).
- `InputValidation` goes; Managed.Assets' `virtual-paths.md` is deleted and its
  readers link to the engine's rules.
- Gate: Managed.Core, Managed.Assets, ContentPipeline, Projects and Assets tests
  that cover names.

**Slice 3. Names in string tables.** VP-03 (formats and cooker).

- The pak format version is bumped. `AssetHeader.name`, `MeshDesc.name`,
  `SubMeshDesc.name`, `ScriptParamRecord` key and string value,
  `external_source_path` and the shader `source_path`, `entry_point` and
  `defines` become string-table offsets.
- The pak and loose writers, the loaders, PakDump, the Inspector and the interop
  readers follow.
- The overflow handling goes: mesh and geometry truncation warnings, the silent
  truncations of materials, scenes, input and physics, and
  `script.asset.name_too_long`.
- Tests: round trips of names longer than 63 bytes, non-ASCII, for each record.
- Gate: Data, Content, Cooker and Interop tests that cover these records.

**Slice 4. One schema definition of a name.** VP-04, VP-03 (schemas and editor),
AID-02.

- One shared definition of the segment rule, referenced by the scene, geometry,
  buffer container, collision shape, physics material, material, input and
  script schemas. `maxLength: 63` and the identifier pattern go.
- JSON Schema cannot count UTF-8 bytes or check NFC, so the definition refuses
  what a pattern can express; the cooker's rule check (slice 1) decides the
  rest, with its codes.
- The editor's embedded schema copies follow. `AssetAuthoring.MaxNameLength` and
  `ContentPipelinePaths.NormalizeSceneDescriptorName` go: `Level 1.oscene` cooks
  as `Level 1.oscene`.
- Gate: Cooker schema tests, ContentPipeline and Assets tests.

**Slice 5. The runtime finds input by key.** AID-05. Independent of the other
slices.

- `InputSystem::GetActionByName` and the `ActionLookup` string constructors go;
  contexts without a key (`InputSystem.cpp:402`) get one.
- Gate: Input tests.

### Phase 2: identity in the editor

**Slice 6a. The asset identity type.** AID-01, VP-09, AID-26 (URI and path
helpers), VP-05 (path comparisons), and the physical path comparisons.

- The defect is not the comparisons but the representation: identity is held as
  `Uri` or `string`, so every caller picks its own comparer, scheme check,
  escaping and `.json` rule (about 225 non-ordinal identity comparisons, 45
  scheme checks, 37 builders of which 24 do not escape, 100 raw `AbsolutePath`
  parsers, ten `.json` strippers). Each rule gets one owner, and a call site no
  longer has a choice to make.
- One value type in Managed.Core holds a canonical, unescaped virtual path (a
  mount, a folder or an asset). Its equality, hash and operators are ordinal,
  defined once; collections keyed by it need no comparer. It sorts ordinally. It
  exposes the one case-folded comparer, used only to refuse case-only siblings
  when a name is authored. It parses an asset URI (scheme, no host, query or
  fragment, unescaping) and writes one (escaping each segment); it answers
  mount, parent, leaf, containment and the engine's virtual path; a JSON
  converter keeps the stored form `asset:///...`.
- It replaces `Uri` as the in-memory identity everywhere in the editor: model
  properties, keys and equality. `Uri` and strings remain only at the JSON,
  route and engine boundaries. The legacy `asset://Mount/x` form,
  `AssetUriHelper`, the `AssetUris` builders, the ten `.json` strippers and the
  unescaped builders go.
- One identity per asset: `AssetEntry` carries one identity (the source path
  without its authoring `.json`, per 1.1) and its source file; `CookedUri`, the
  two `GetLogicalKey` copies and the case-insensitive lookup fallback go.
- Managed.Core owns the identity rule (strip the authoring `.json`).
  `AssetFileTypes` owns the type mappings (`.lua` to `.oscript`) and calls it.
  `ProjectAssetFiles` stays the one resolver between an identity and a file,
  since it knows the mounts, and takes and returns the type.
- Physical paths get the same treatment: one Managed.Core type owns Windows case
  and full-path normalization for file system paths, and the hand-written
  `OrdinalIgnoreCase` and `GetFullPath` comparisons go.
- `Oxygen.Editor.Assets`' `AssetIdentity` keeps only its grouping rules, on the
  type.
- Format types spell references as the engine reads them, at their own boundary.
- Slice 6 lands in three commits. 6a is the source migration; its test projects
  are migrated in 6b, so between the two the test projects do not compile.

**Slice 6b. Tests of the identity type.**

- Every test project moves to the identity and physical path types; tests of
  removed behavior (old relative-path route URLs, URI validation codes that can
  no longer fire) go.
- Tests: round trips of `%`, `#`, `?`, space and non-ASCII names; ordinal
  equality; case-folded sibling refusal; physical path equality.
- Gate: Managed.Core tests and the tests of every changed caller.

**Slice 6c. Built-in assets in the engine pak.**

- Built-in assets are ordinary assets with ordinary virtual paths under
  `/Engine`, file type included, and keys derived from those paths like every
  other asset: `/Engine/Generated/BasicShapes/<Name>.ogeo` for the shapes a
  picker offers, `/Engine/Generated/Materials/Default.omat` for the default
  material, and `/Engine/Internal/...` for engine-internal assets such as the
  arrow gizmo, which pickers hide by folder. The authoring categories go; the
  folder carries the distinction.
- The default material's fixed key (`MAT_DEFAULT_____`) goes: geometry loading,
  the importers' fallback, Vortex and scripting use the key of
  `/Engine/Generated/Materials/Default.omat`.
- Nothing special-cases built-ins. `BuiltinGeometry`'s URI API
  (`asset:///Engine/Generated/BasicShapes/` prefix, identity resolution,
  in-memory cache) goes, and so do the interop's in-memory geometry
  (`SetGeometryCommand`) and default material, its refresh exclusion, and its
  `asset:` and `.omat.json` stripping (`SceneAssetRequests`).
- The engine build cooks them once: an ImportTool command (replacing
  `builtin-catalog`) cooks a loose `/Engine` root with procedural geometry
  descriptors, and PakTool packs it with an embedded browse index and its own
  source key into `engine.pak`, installed under `share/oxygen` in each
  configuration of the SDK. `PathFinderConfig` names it.
- Every runtime mounts the engine pak first, so later mounts override it: the
  Player, DemoShell, RenderScene and the editor preview. A prepared mount set is
  typed: `IAssetLoader::PrepareMountsAsync` takes `ContentMount`s, each a loose
  cooked root or a pak file, lowest precedence first, so the editor swaps paks
  and loose roots atomically. The Player's launch mounts are `ContentMount`s,
  and every Play remounts them all, paks included; the rule that changed pak
  mounts need a new Player process goes.
- Environment textures are addressed by virtual path like geometry and
  materials, so textures in any mounted pak work; the cooked-root and project
  mount texture binding goes. Paks carry no texture descriptors today, so
  PakTool records each texture descriptor's virtual path in the browse index
  (path to texture-table index) and PakFile resolves it (pak format version
  bump; content is recooked).
- Materials and scene environments use textures other cooked sources hold (the
  engine pak, user paks, cooked libraries) without copying them: the reference
  table gains an external texture binding naming a texture key (the engine key
  of the texture's virtual path), the cook binds a texture its own output holds
  locally and one a cooked context root holds by key, the runtime resolves the
  key in the newest mount holding it, and a pak build requires the texture in
  the pak, its bases or its dependencies. Material jobs take cooked context
  roots. The editor's catalogs list cooked textures of paks and loose libraries
  with their texture-table facts, so the texture pickers offer them.
- The editor: a managed pak reader (browse index) next to the loose index
  reader; a pak is a mount kind next to local folders, and the `/Engine` pak is
  always present and comes from the SDK:
  - `Project.oxy` declares user paks in `PakFileMounts` (name, absolute `.pak`
    path); `PakFile` is a cooked content order kind. A mount name names one
    library across folders and paks, and project validation checks the file.
  - A user pak is a cooked library: a shared read lease holds it, its catalog
    digest is its fingerprint, cooks resolve references into it through the
    native context roots without copying them, and publication roots record
    their container (`LooseFolder` or `PakFile`; older publications only had
    folders). Mounting checks a pak's identity and catalog digest against the
    publication and refuses a replaced pak.
  - Runtimes receive typed roots (`RuntimeCookedRoot.Kind`), carried through the
    interop as `CookedRootManaged`. The Player tells a pak by its `.pak`
    extension, so pak mounts must name `.pak` files.
  - The Content mounts dialog lists the engine content as a fixed, read-only
    last row and adds pak files ("Pak file" location, `.pak` picker). The
    Content Browser shows the engine pak and each declared pak as read-only
    roots whose folders are their assets' virtual folders: `/Engine` folders
    relative to the engine mount, a user pak's under its mount name. Content
    priority lists paks with folders; the engine content is always lowest and is
    not listed.
- Built-in machinery goes: `BuiltinCatalogDiscovery`, `BuiltinGeometryCatalog`,
  `BuiltinAssetCatalog`, `GeneratedAssetCatalog`, project copies
  (`VerifiedBuiltinSource`, `BuiltinOrigin`),
  `ProceduralGeometryDescriptorService`, provenance built-in owners and
  generated descriptor inputs. Project cooks depend on `/Engine` assets and stop
  producing copies.
- References in the old spelling (`asset:///Engine/Generated/BasicShapes/Cube`)
  are rejected; repository scenes are rewritten. Publications holding built-in
  products are recooked (version bump), not migrated.
- Layered paks: a pak declares two kinds of lower layers in its catalog (catalog
  version bump). Patch bases are the exact contiguous stack a patch's deltas
  apply to. Dependencies are paks whose assets it references without containing
  them, such as `engine.pak`. A dependency must be mounted below, in declared
  order, with the identity, content version and catalog digest the pak was built
  against; other layers may sit between. PakTool takes `--dependency-pak` for
  both commands and `--base-pak` for patches only. The planner resolves
  references into bases and dependencies (type-checked, never copied), and the
  runtime enforces both rules at mount. ImportTool `batch` takes
  `--cooked-context-root` (repeatable, beneath the manifest's roots), and the
  example scripts find the engine pak themselves: the SDK's, or the one the
  selected build tree records, built as a prerequisite.
- In the editor, `engine.pak` is an SDK artifact. The cooking artifact inventory
  verifies and leases it like the tools and schemas, so its content identity
  joins the cook fingerprint. Cooked libraries gain a pak root: a shared read
  lease, records from `PakAssetCatalog`, the catalog digest as fingerprint,
  dependencies from the native report (which reads paks), and native validation.
  The engine pak is the lowest library and the first native context root. It
  stays out of publication bindings, because every runtime already mounts it as
  its permanent base. A reference into `/Engine` is a library reference:
  published as it is, resolved by the context root, never cooked into the
  project.
- `CookedAssetMetadata` names its container (a loose root or a pak); the
  descriptor path and digest exist only in a loose root. Pak assets have the
  Packaged state: read-only, no cook or edit actions, and a tooltip that names
  the pak. The badge is Engine for `engine.pak` and Pak for user paks. The
  Generated state, its Built-in badge and the last-known catalog notices go.
- Pickers list `/Engine/Generated` from the catalog and hide `/Engine/Internal`.
  Every asset picker is one shared control, `AssetPicker` in
  `Oxygen.Editor.Pickers`, which groups choices as Engine, Packages and Content
  in that order and hides empty groups. Slot inventories of engine geometry come
  from the native geometry report on `engine.pak`, like any cooked geometry.
  `EngineAssets` (Managed.Assets) names the engine folders, basic shapes and
  default material the editor uses. `AssetPath.Generated` and `IsGenerated` go.
- Gate: Data, Content and Cooker engine tests; interop native tests;
  Managed.Assets, ContentPipeline, Assets, ContentBrowser, LivePreview, Runtime
  and WorldEditor tests.

**Slice 7. References held as identities.** AID-03.

- `SceneReferencesData.ExtraAssets` and the material texture bindings hold the
  identity type; bare paths remain only inside the JSON the engine reads.
- Gate: Scenes, Materials and World tests.

**Slice 8. File types describe containers.** AID-21, AID-24, AID-26 (extension
lists), VP-05 (extension matching).

- `AssetFileTypes` matches extensions ordinally, as the engine does (VP-05).
- `AssetFileTypes` states, per source type, one asset or a container and the
  asset types it produces; `.otex` gets its type.
- `RelocationPaths.CompoundExtensions`,
  `AuthoredReferenceFormat.DescriptorExtensions`, both `IsModelSource` and the
  inline lists in `ImportedSourceIndex` and `ProjectAssetKeyIndex` come from it.
- Copying `Player.input.json` gives `Player (2).input.json`.
- Gate: Managed.Assets, ContentPipeline and Assets tests.

**Slice 9. Format readers.** AID-16, AID-17.

- `TextureSource` reader and writer in Managed.Assets replace the four texture
  parsers; a texture with `sources[]` reports its images (test reproduces the
  bug).
- `CookSourceAnalyzer` reads materials through `MaterialSourceReader`; one scene
  id reader replaces `SceneFiles.ReadIdAsync`,
  `AuthoredReferenceFormat.ReadSceneId` and
  `ProjectCreationService.TryReadSceneId`.
- Gate: Managed.Assets, ContentPipeline, Scenes, Projects and Textures tests.

### Phase 3: provenance, containers and collisions

**Slice 10. Provenance in the import report.** AID-12 (report and map), AID-14.

- The engine's import report lists each source's products: key, virtual path,
  asset type. The report's version is bumped.
- `NativeImportReportReader` and its schema read them; `CookProvenance` becomes
  the one map, source to products and product to source.
- `OutputsBySource`, `ContentCookInput.OwnsOutput`, the folder claims of
  `CreateCookedAssets` and `ProjectAssetKeyIndex.EnumeratePaths` read the map.
  Input scans go in slice 12, model prefixes in slice 15.
- Gate: Cooker import report tests, ContentPipeline tests.

**Slice 11. The cooked layout is the identity.** AID-06, AID-20.

- Cook inputs and their provenance name a source by its identity, not its file
  (`Red.omat`, `Player.input`), and the consumers' `.json` strippers go.
- A single-asset job's request carries its identity (`virtual_path`, required);
  the cooker writes the descriptor there. The per-type directories of
  `LooseCookedLayout::DescriptorDirFor` go for those jobs, with
  `ContentImportManifestBuilder.CreateJobLayout`'s overrides. Containers keep
  their layout until their products are named under them: the input folder goes
  in slice 12, the model folders in slice 15.
- ImportTool, engine.pak and the example cooks place descriptors at their
  identity; the examples state the identities they already had.
- `scripting_pipelines.md` and `scene_work_pipeline_v2.md` state 1.1.
- Gate: Cooker layout tests, ContentPipeline tests, the examples' cooks.

**Slice 12. Input sources are containers.** AID-04, AID-08 (naming), AID-11,
AID-18, AID-19, AID-22, AID-27, AID-12 (input).

- One schema with `actions` and `contexts` optional. The standalone action
  schema, `.input-action.json`, `AssetFileTypes.InputActionFile`, the second
  template and `ProjectScriptTooling`'s copy go.
- The document kind is decided once; schema validation reports its own errors.
- Products are named under the container: `Player.input/Jump.oiact`. The
  layout's input folder and the editor's input folder override go.
- An action reference is a local name or a virtual path, resolved like a scene
  reference. `BuildMountedActions`, its `cooked_context_roots` loop and the
  value-type-only redeclaration check go.
- The editor's three refusals (`RejectInputFilesWithForeignActions`,
  `InputFileCheck`, `ScriptAnalysisService`) become one check that a path names
  an action a project source declares (`InputSourceFiles.CheckActionPaths`).
  `InputContextSources.FindDeclaredPriority` and `InputSourceIndex.Find` read
  the declaring source, which a product's identity names: the map only knows
  what was cooked, and a declaration is authoring state.
- An action path is a reference: the source analysis reports it, discovery
  resolves a product of a project source to that source (so the manifest orders
  the jobs), and input jobs take `cooked_context_roots` like scene jobs. The
  cooker's scene reference resolution becomes the shared
  `ResolveMountedAssetReference`.
- The examples' input sources follow. `input-cooking-architecture.md` states
  1.3, 1.4 and 1.6.
- Tests: two sources in one folder declaring `Jump` cook to two keys; a
  cross-source reference by path; moving an input source keeps a scene's context
  reference.
- Gate: Cooker input tests, Input and Content input loader tests,
  ContentPipeline, Managed.Assets, Projects and Scripting tests.

**Slice 13. Input reader and product renames.** AID-15, AID-23.

- `InputSource` reader and writer in Managed.Assets, with the engine schema
  embedded and positions for problems, replace `InputFileReferences`,
  `InputSourceIndex`'s parser and `InputFileCheck`'s parser.
- Renaming an action or context is a relocation operation: the map gets the old
  and new product identity and the referrers are rewritten. The Input module of
  S1.8 calls it from its editor.
- Gate: Managed.Assets and ContentPipeline tests.

**Slice 14. Model import naming.** VP-07, VP-14; clashes 1, 2, 3, 6, 7, 8 and 9
of 3.3.

- `Naming.cpp` implements the naming rule of 1.2 over `VirtualPath.h` (`ToNfc`,
  `FoldCase`, the segment checks): authored names kept, NFC, refused characters
  and invalid sequences replaced, the whole-name fixes (leading `.`, leading or
  trailing space, trailing dot, device names), shortening to the 255-byte leaf,
  the kind fallbacks, uniqueness per container and type with Unicode simple case
  folding, `_N` probing. Scene node names stay verbatim and unique by exact
  comparison.
- The `G_` and `M_` prefixes and the naming policies go: `NamingStrategy` and
  its implementations, `ImportOptions::naming_strategy`, and the `naming_policy`
  key of the import manifest schema, `SceneImportSettings`, the retained model
  recipe, `import_models.ps1` and the examples README. FBX's default material
  becomes `Default`. The `<SceneName>/` namespace stays until slice 15, which
  replaces what it fixes.
- glTF and FBX texture work items are keyed by source (glTF image index, FBX
  file path or embedded element, and usage) and produced sorted by source key;
  buffers already follow their meshes' document order. Texture and buffer names
  stay `<stem>_<16 hex>` until slice 15, where they become product names; this
  slice adds no registry kind that nothing reads.
- Pinning tests rewritten to the rule. New tests: two images with one name and
  usage give two textures; a fixed texture order; `Paint.001`, `Wood-Trim`,
  `Wood Mat` and `SM_Hull` kept as spelled; a leading `.`, a trailing space and
  dot, `CON` and `NUL.v2`, a non-NFC name, invalid UTF-8 and a name over 255
  bytes; unnamed meshes and materials against an authored `Mesh_0`; scene node
  names that keep `/` and stay unique.
- Renaming a model product, and applying a convention through renames, is
  [#79](https://github.com/abdes/DroidNet/issues/79), outside this plan.
- Gate: Cooker glTF, FBX and naming tests, ContentPipeline import tests.

**Slice 15. Model products under the container.** AID-07, VP-06, VP-13, AID-12
(models), AID-10 (model checks); clashes 4, 5 and 11 of 3.3.

The slice is contract and debt work only. It moves model products under their
model, removes the mechanisms that layout replaces and keeps the tree working;
it adds no presentation.

Decisions:

- A model is identified by its source file's virtual path, and its products are
  flat under it: `/Content/Vehicles/Ship/Ship.glb/Paint.omat`. No type folders
  or groups appear in an identity; grouping and filtering by type are
  presentation.
- A model's scene is named after the model file without its extension:
  `Ship.glb/Ship.oscene` (1.2). Godot and Unity name a model's scene or prefab
  after the file; a glTF scene name or an index (`Scene_0`) is not used.
- The editor imports a model from outside the project into the folder the user
  chooses, as a bundle folder named by the user (default: the file's stem) that
  keeps the bundle's relative layout: `Content/Vehicles/Ship/Ship.glb` with its
  `.bin` and textures beside it. A model already under an authoring mount is
  used where it is. `SourceMedia/DCC` is no longer used for models.
- Tools name a model's identity explicitly, as every other job does since slices
  11 and 12; the cooker never infers it.
- The Content Browser does not list container products, of models and input
  sources alike: a container shows as its one source file (`Ship.glb`,
  `Player.input`). Nothing a user does with a product goes through its row:
  pickers list products from the catalog, a slot override is stored on the scene
  node, and no asset is dragged out of the browser. The one loss is Edit on an
  input action or context, which opened the input file at the declaration's
  line; the input file itself opens at its first line. The container view, the
  read-only treatment and every import feature are
  [#80](https://github.com/abdes/DroidNet/issues/80), built after the plan. A
  later slice that implements part of #80, or a stand-in for it, is rewritten.

Engine (cooker):

- `gltf` and `fbx` jobs require `virtual_path`, the model's identity
  (`ReadJobVirtualPath` refuses it today, `ImportManifest.cpp:543`); it must end
  in the model's source extension and lie under the job's mount. `ImportRequest`
  carries it to the adapters, the pipelines, `ImportSourceAnalysis` and the
  scene inference of `BatchCommand.cpp:178`. `SceneImportSettings` gains it and
  `RetainedModelImport::MakeRecipe` writes it.
- One function names a model product, `<model identity>/<name>.<ext>`, used by
  analysis and production for materials, geometry, scenes and textures. The
  scene's name is the model's stem, so the scene naming kind and its
  `Scene_<index>` fallback go. Texture names come from the registry (1.2) with
  their usage, through a texture registry kind; the test of `a/diffuse.png` and
  `b/diffuse.png` pins the `_1` probe.
- A model's buffer sidecars are files, not assets
  (`ResourceDescriptorEmitter::EmitBuffer` gives them no virtual path); they are
  named after their mesh, level of detail and kind under the model's folder in
  the cooked root.
- The `<SceneName>/` namespace goes from `NamingContext` and the adapters, with
  `AssetNamingTest.NamespaceParticipatesInPortableStorageIdentity`.
- The hashed names go: `BuildStem`, `SanitizeStem` and the two-argument
  `TextureDescriptorRelPath`/`BufferDescriptorRelPath`
  (`LooseCookedLayout.cpp:36-98`), the hashed fallback of
  `ImportRequest::GetTextureDescriptorRelPath`, `ChooseStableId`'s naming use in
  `WorkDispatcher`, and the README line that documents them
  (`Tools/ImportTool/README.md:107`). Their layout tests go.
- The layout's per-type folders go: the material, geometry, scene, texture,
  buffer and descriptors folders and the unused physics helpers of
  `LooseCookedLayout`, the `layout` keys of the import manifest schema except
  `virtual_mount_root` and `resources_dir`, their parsing
  (`ImportManifest.cpp:818-843`) and `MakeRecipe`'s copy.
- Material slot provenance is keyed by the geometry's name inside its model, not
  by the `AssetKey` of its virtual path (`MaterialSlotAllocation.cpp:31-52`,
  `RetainedModelImport.cpp:679`). Slot ids then survive moving or renaming the
  model; the inventory's geometry key is recomputed on each cook. The anchor
  loses the `<SceneName>/` prefix.

Engine (content):

- The cooker's hashed texture scan goes: `FindTextureResourceDescriptorPath`
  (`Content/TextureDescriptorPath.h/.cpp`), its use in
  `TextureReferenceResolver` with `texture_descriptor_ambiguous`, and
  `ResolvesHashedTextureDescriptorVirtualPathAndEmitsMaterial`. A texture
  reference resolves to the descriptor at its identity or fails.

Examples and tools:

- In-tree model jobs (`rotating-gltf`, `sdk-furniture`, `sdk-lantern`) name
  their identity as the model's path under the content root
  (`/.cooked/glb/backpack.glb`). `rotating_gltf.scene.json` references the model
  scenes by their new identities.
- `import-sources.schema.json` gains an optional `virtual_path`, default
  `/.cooked/Models/<name><extension>`; `import_models.ps1` writes it into the
  recipe.
- `play_scene.ps1` lists the `.oscene` descriptors of each mounted root by their
  path, which is their identity, instead of scanning `Scenes` folders.
- DemoShell names the model's identity when it imports, as every model job must:
  `<Virtual Root setting>/<stem><extension>`, passed to `MakeRecipe`
  (`ContentVm.cpp:310-322`). Its layout settings follow the layout's fields: the
  Descriptors Dir, Scenes, Geometry and Materials Subdir rows of the Content
  Loader panel with their `demo.content.layout.*` commands
  (`ContentLoaderPanel.cpp:270-286`), and their settings keys
  (`ContentSettingsService.cpp:110-113`, `:297-308`, `:321-324`). Virtual Root
  and Resources Dir stay.

Editor:

- Ownership: a model product's container follows from its identity, as for input
  products; `AssetFileTypes.ContainerOf` learns model containers (a `.glb` has
  no authoring suffix, so the container is the parent path itself when it names
  a model). `ImportedSourceIndex` goes. The `ForeignSource` branch of
  `ContentCookInput.OwnsOutput`, `CookSourceAnalyzer`'s output namespaces,
  `AssetCookStatusReader`, `ImportReplacement`, the inspection scopes and the
  import collision check of `ContentPipelineService.ImportedSources` read the
  identity and the provenance map. A model product is cooked only by its model:
  an authored source that produces the same identity is a collision (slice 16
  makes that one check).
- Import settings: `NativeSceneImportSettings` loses `OutputDirectory`,
  `OutputDirectories` and `OutputPrefixes`; its schema version is bumped and an
  older version is refused with a code. `SceneImportTarget` goes and
  `ContentImportLayout` keeps only `virtual_mount_root`; the manifest builder
  passes the model's identity.
- Import flow: the destination the dialog asks for is where the model's bundle
  goes; `ImportSourceRetention` copies the bundle there instead of
  `SourceMedia/DCC`, refusing an existing destination. The dialog's preview
  states that path. Nothing else in the dialog changes.
- Relocation: moving or renaming a model maps it as a container
  (`AssetRelocationPlanner.cs:64-70`), so referrers to its products follow; the
  provenance re-key keeps its slot ids. The output-group moves go
  (`ImportGroupMove`, `GroupMoves`, `ExpandImportGroups`, `CheckGroupOverlaps`,
  `RelocationPaths.ImportTypeFolders`, `PreviousGroups`, the Rename Output Group
  command and `AuthoredReferenceFormat.Relocate`'s output directory rewrite).
  The refusals that keep a model with the files it references by relative path
  stay (`AssetRelocationPlanner.cs:104`, `:119`, `:126`). Moving, renaming or
  deleting one product of a model is refused with a code: a model product has no
  authored place until #80 gives it one.
- Content Browser: the row filter (`AssetsLayoutViewModel.cs:380-387`) drops a
  row whose identity has a container (`AssetFileTypes.ContainerOf`), and the
  cooked and pak folder trees (`CookedFolderTreeItemAdapter.Build`) stop at a
  container's segment, so no `Ship.glb` or `Player.input` folder appears. The
  input product branches of the browser go with the rows they served:
  `AssetsViewModel.FindCodeSource`'s action and context case and
  `AssetDetailsViewModel.BuildSourceFacts`' "Declared in" fact.

Migration (data is edited, never read through compatibility code):

- The four local retained records (`Examples/Content/imports`) gain their job's
  `virtual_path`, lose the layout folders, and have their provenance anchors
  stripped of the `<SceneName>/` prefix; then they are re-imported.
- Editor projects: a model under `SourceMedia/DCC` is moved out to a project
  folder and its import settings file loses `OutputDirectory` and takes the new
  schema version. The owner migrates their own projects.

Tests:

- Cooker: two models in one cooked root with the same material names;
  `a/diffuse.png` and `b/diffuse.png` in one model give `diffuse` and
  `diffuse_1`; a model job without `virtual_path`, or with one that does not end
  in the model's extension, is refused; a model's scene is named after the
  model; moving a model's identity keeps its material slot ids.
- Editor: `ImportedOutputOwnershipTests` and
  `RetainedSourceCookTests.ImportDestinationCollisionPreservesExistingSourceOutputs`
  are rewritten to identity ownership; the output-group relocation tests go;
  new: a model imported from outside lands in the chosen folder, moving a model
  rewrites references to its products, renaming one product is refused, a
  material slot override on a model's mesh survives moving the model, the
  material picker lists a model's materials, and the Content Browser lists
  neither a model's nor an input source's products, in the project or a pak.
- Gate: Cooker layout, model, naming and ImportTool tests, Content texture
  tests, ContentPipeline and Assets tests; every example scene and retained
  import cooks into one cooked root and packs into one pak.

**Slice 16. One collision policy.** AID-08 (policy), AID-09, AID-10, VP-05
(cooker); clash 10 of 3.3.

- The loose index format records each entry's producing source (version bumped).
  The emitter and the index writer replace a source's own asset and refuse a key
  from another source and a case-only sibling, each with its code.
  `kWarnReplace` goes.
- `LooseCookedWriter.h` and `input-cooking-architecture.md:378` state the
  policy.
- The editor's per-family checks (`ScriptSourceNames.FindCollision`,
  `ScriptAnalysisService.FindCollisions`, `CookProvenance.FindBufferOwner`)
  become one check over the map, covering descriptors and input products.
- It follows slices 12 and 15 so that the new error never meets content whose
  old naming shared a key.
- Gate: Cooker emitter and loose index tests, Content loose index tests,
  ContentPipeline and Scripting tests.

What can still collide after slice 15, and where each case is caught:

- A model's products cannot collide with authored assets: they are named under a
  file, and no folder of that name can exist beside it.
- Two scripts of one name (`move.lua`, `move.luau`), and two sources whose
  identities differ only in case, are refused where they are authored
  (`SourceCollisions`, used by relocation and script analysis).
- Two geometry descriptors that declare one buffer, and any output another
  source still in the project produced, are refused by the cook: discovery keeps
  one producer per identity, folded for case, over the sources it cooks and the
  accepted facts of the others (`OXE.ASSETCOOK.IdentityCollision`, both
  sources). A source that was moved or deleted produces nothing; its outputs are
  removed before the cook writes the root.
- The model-only output check of the import goes: a model re-cooking its own
  products replaces them (1.6).
- `DedupCollisionPolicy` in the texture, buffer and physics emitters is not an
  identity policy: its key is a resource dedup signature. It stays out of this
  slice.

In the cooker, the index records each entry's producing source (format 6). A
job's source is derived as its identity's folder plus the source file's name; a
sidecar's output keeps the producer of the scene it completes, from the cooked
root, the scene in flight or its context root. A source re-cooking its identity
replaces it; another source, or an identity differing only in case, is refused
(`import.identity_collision`).

**Slice 17. Container status.** AID-13 (status).

- A product's status is its container's, from the map; `CookInputResolver`
  resolves products instead of throwing "Unsupported cook dependency". The
  feed's rows and their grouping are not touched: how containers and their
  products are presented is [#80](https://github.com/abdes/DroidNet/issues/80).
- Gate: ContentPipeline and Assets tests.

**Slice 18. Relocation by reference fields.** AID-25.

- Each format type names its reference fields; the rewriter visits them, and
  `AuthoredReferenceFormat.TryParseIdentity`'s string matching goes.
- Gate: ContentPipeline and Managed.Assets relocation tests.

### Phase 4: documents

**Slice 19. Documents.**

- The contract becomes the [asset identity LLD](../lld/asset-identity.md); the
  register and the clash table go, their rules now in the LLD and the tests.
- The [asset family modules LLD](../lld/asset-family-modules.md) takes
  `InputSource`, the action and mapping context model and the provenance map.

## 3. Coverage

| Item           | Slice      | Item                 | Slice          |
| -------------- | ---------- | -------------------- | -------------- |
| AID-01         | 6          | VP-01                | 1, 2           |
| AID-02         | 4          | VP-02                | 1, 2           |
| AID-03         | 7          | VP-03                | 3, 4           |
| AID-04         | 12         | VP-04                | 4              |
| AID-05         | 5          | VP-05                | 1, 2, 6, 8, 16 |
| AID-06         | 11         | VP-06                | 15             |
| AID-07         | 15         | VP-07                | 14             |
| AID-08         | 12, 16     | VP-08                | 1, 2           |
| AID-09         | 16         | VP-09                | 6              |
| AID-10         | 15, 16     | VP-10                | 1, 2           |
| AID-11         | 12         | VP-11                | 1              |
| AID-12         | 10, 12, 15 | VP-12                | 1, 2           |
| AID-13         | 17         | VP-13                | 15             |
| AID-14         | 10         | VP-14                | 14             |
| AID-15         | 13         | 3.3 clashes 1-3, 6-9 | 14             |
| AID-16         | 9          | 3.3 clashes 4, 5, 11 | 15             |
| AID-17         | 9          | 3.3 clash 10         | 16             |
| AID-18, AID-19 | 12         | 3.3 clash 12         | unchanged      |
| AID-20         | 11         | Section 4            | 12, 19         |
| AID-21         | 8          |                      |                |
| AID-22         | 12         |                      |                |
| AID-23         | 13         |                      |                |
| AID-24         | 8          |                      |                |
| AID-25         | 18         |                      |                |
| AID-26         | 6, 8       |                      |                |
| AID-27         | 12         |                      |                |
