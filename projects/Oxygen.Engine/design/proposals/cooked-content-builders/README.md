# Cooked content builders

Status: implemented

Last updated: 2026-11-01

Engine tests need cooked content: loose cooked roots, paks, their catalogs and
browse indexes, the asset descriptors inside them, pak build requests and
model import records. This proposal defines one builder per artifact, composed
from the product's own serde routines, so a format change edits the product,
never the tests. The builders work in memory: they never write or read a file.

It uses the vocabulary of [cooked sources](../cooked-sources/README.md): a
**mount** is the first segment of a virtual path (`Game`, `Engine`, `Props`),
a **cooked source** is a loose cooked root or a pak holding one mount's
content, whether a game's or a shared pack's.

## Contents

- [Problem](#problem)
- [Model](#model)
- [Builders](#builders)
- [Descriptor builders](#descriptor-builders)
- [Invariants](#invariants)
- [Product seams](#product-seams)
- [Pipeline tests](#pipeline-tests)
- [Malformed artifacts](#malformed-artifacts)
- [Hardening](#hardening)
- [What it replaces](#what-it-replaces)
- [Order](#order)
- [Decisions](#decisions)

## Problem

A test that needs a pak or a loose root used to write one by hand: forged
descriptor bytes, a pak through a second encoder of the pak format,
`data::PakCatalog` literals, request structs filled field by field, and a dozen
file-local `WriteRoot`/`Material` helpers. A test that needs a scene with
nodes, renderables or script slots still lays out `SceneAssetDesc`, its tables
and its string table by hand, about seventy lines each time, and a test that
needs a texture descriptor file still forges its bytes. Each restates the
format. When the format changes (catalog version 5, a mandatory browse index,
one mount per pak, a sidecar that records its scene's hash), every
restatement breaks separately, and each one is fixed by hand.

## Model

A builder describes cooked content the way the engine defines it, never the
way a test happens to need it:

| Concept        | Engine definition                                                                                                      | Builder term                                                                                                 |
| -------------- | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Cooked source  | One mount's cooked content, a loose cooked root or a pak                                                               | `LooseCookedRoot("Props")`; every leaf it takes is relative to `/Props`                                      |
| Asset          | A typed descriptor at a virtual path; its key derives from the path                                                    | `.Material(leaf)`, `.Geometry(leaf)`, `.Scene(leaf, scene)`, `.PhysicsSidecar(leaf, scene_key)`, `.Add(...)` |
| Asset contents | The records of a descriptor: a scene's nodes and components, a sidecar's bindings                                      | `SceneBuilder`, `PhysicsSidecarBuilder` ([descriptor builders](#descriptor-builders))                        |
| Resource       | A texture, buffer, script or physics resource in the source's tables                                                   | `.AddTexture(payload)`, `.AddBuffer(...)`, `.AddScript(...)`, `.AddPhysics(...)`, each returning its row     |
| Named texture  | A virtual path bound to a texture row (a texture descriptor file)                                                      | `.DescribeTexture(leaf, row)`                                                                                |
| Reference      | An asset's reference to a resource row or to another asset, possibly under another mount                               | A descriptor builder's references, by row or by key                                                          |
| Several mounts | A loose root whose content lies in more than one mount (development roots)                                             | `LooseAsset::mount`, per asset                                                                               |
| Deletion       | A pak's tombstone of a lower asset                                                                                     | `CookedPak(...).PatchOf(base)`: what the base holds and the patch no longer does                             |
| Pak            | Cooked content of one mount with its catalog, browse index and requirements, over dependencies and, for a patch, bases | `CookedPak` of one or more `LooseCookedRoot`s, `.DependsOn(engine)`, `.PatchOf(base)`                        |
| Model record   | `<model identity>.import.json` in the model's mount                                                                    | `ModelRecord("/Props/Models/Truck.glb").Source("Truck.fbx")`                                                 |

A test names only what it is about: a material with one texture, a scene with
two nodes. Every other field has the value a cook writes.

## Builders

All builders live in one engine test kit, `Oxygen.Cooker.Testing`
(`src/Oxygen/Cooker/testing/`, namespace `oxygen::cooker::testing`), which
any engine test program links: Content, Cooker, PakTool, Inspector, Player and
the examples' tests, including the UI tests (which therefore build only with
`OXYGEN_BUILD_TESTS`). Their output is the test `MemoryFileSystem` (the
in-memory implementation of the Cooker file system port) or bytes.

| Builder                 | Produces                                                                | From the product                                                                                                          |
| ----------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `LooseCookedRoot`       | A loose cooked root: index, descriptors, resource tables and data files | The descriptor builders, resource descriptor structs, the `ResourceDescriptor` codec, the loose index encoder             |
| `SceneBuilder`          | The descriptor of a scene                                               | `SceneAssetDesc`, the node and component records, `DescriptorStringsBuilder`, `MakeDescriptorBytes`                       |
| `PhysicsSidecarBuilder` | The bindings of a scene's physics sidecar                               | `PhysicsSceneAssetDesc`, the binding records; the scene's own bytes for its key, node count and hash                      |
| `CookedPak`             | The bytes of a pak of one mount                                         | `PakPlanBuilder` and `PakWriter` over its loose roots, in memory                                                          |
| `PakWorkspace`          | `PakBuildRequest`s: full or patch, sources, dependencies and bases      | The request types; dependencies and bases read through `PakFile` from built paks; each request a distinct source identity |
| `PackedPak`             | A read view of a built pak                                              | `PakFile` over a `MemoryFile` or the workspace's storage                                                                  |
| `ModelRecord`           | A model import record at its identity                                   | The record's serializer                                                                                                   |
| `CookedBytes`           | A named defect applied to a built artifact's bytes                      | The format structs (`PakHeader`, `PakFooter`, the catalog and index headers)                                              |
| `DescriptorDefects`     | A named defect applied to a built descriptor's bytes                    | The descriptor structs (`SceneAssetDesc`, its tables, `PhysicsSceneAssetDesc`)                                            |

Every byte a builder emits comes from a product routine. `CookedPak` decides
nothing about the pak format: it writes its loose roots into a workspace and
packs them with the planner and the writer, so a pak always follows the rules
`PakFile` checks (payloads on 4 KiB boundaries, no overlap, the catalog before
the footer, a physics sidecar that matches its scene); the kit's own tests open
every built pak with the real `PakFile`.

`CookedPak` derives requirements and deletions the way a cook does: a pak
whose asset references `/Engine/Default.omat` and that `.DependsOn` a pak of
`Engine` requires that asset of `Engine`; a `.PatchOf` a base deletes every
asset of the base its roots no longer hold. Whether the planner resolves
references into exactly those requirements stays the planner's own test, a
[pipeline test](#pipeline-tests).

The workspace (`PakWorkspace`) is the request builder: `FullRequest` and
`PatchRequest` give deterministic requests over its storage, each with an
identity of its own; `Dependency(pak)` and `Catalog(pak)` read a built pak
back as a dependency or a base; `Pack`, `PackOrThrow` and `Build` run the
pipeline and print every diagnostic on failure.

## Descriptor builders

An asset's descriptor is built by the builder of its type, which computes
every offset, count, entry size and string, and takes only what a test is
about. A builder's methods return handles, so a test cannot name a node,
a component or a slot the descriptor does not hold.

```cpp
auto game = LooseCookedRoot { "Game" };
const auto crate_geometry = game.Geometry("Props/Crate.ogeo");

auto main = SceneBuilder { "Main" };
const auto root = main.Node("root");
const auto crate = main.Node("crate", root); // its parent
main.Renderable(crate, crate_geometry);
main.ScriptSlot(crate, game.KeyOf("Scripts/Spin.oscript"),
  { { "speed", 3 }, { "label", std::string("fast") } });
main.Override(crate, OverrideId::kGeometrySurfaceMaterial,
  { .as_asset = game.KeyOf("Materials/Red.omat") }, surface, layout_revision);

const auto scene = game.Scene("Scenes/Main.oscene", main);
auto bindings = PhysicsSidecarBuilder {};
bindings.RigidBody(crate, shape_key, physics_material_key);
game.PhysicsSidecar("Scenes/Main.opscene", scene, bindings);
```

- **`SceneBuilder`** holds the scene's name, its nodes (each with a name, a
  parent, a transform and flags, its node ID derived from its stable identity
  `/Main/root/crate`), its components (renderables, cameras, lights, local
  fog volumes, scripting slots with typed parameters, overrides and LOD
  policies), its composition (instances, changes and script changes below
  them by node path, starting input contexts, a starting camera, the model
  scene flag), its environment block and the texture rows its records bind.
  `Bytes()` lays out the descriptor the way the scene and scripting cookers
  do: component tables sorted as they sort them and in component type order,
  one string table, script slots and their parameters, the environment block
  last, and the header's content hash; `References()` are the references the
  cook records. A handle of another builder, a repeated node, flags or a
  record size a cook never writes, or a scene the product reader refuses
  throws, where it is added or at `Bytes()`; handles are tied to their
  builder by a `SceneBuilderId`.
- **`LooseCookedRoot::Scene(leaf)`** keeps its meaning, a scene with only its
  root node; `Scene(leaf, builder)` adds the built scene.
- **`PhysicsSidecarBuilder`** holds the sidecar's bindings (rigid bodies,
  colliders, characters, soft bodies, joints, vehicles, aggregates) by node
  handle, all of one scene. `LooseCookedRoot::PhysicsSidecar(leaf, scene_key,
bindings)` takes the scene the root already holds, refuses bindings of
  another scene's nodes, and records what a cook records of it: its key, its
  node count and the SHA-256 of its bytes, so the planner's pairing check
  always holds. `PhysicsSidecarDescriptor(scene_key, scene_bytes, bindings)`
  builds the same descriptor for a scene stored in another source.
- **Materials and geometry** keep their builders (`MaterialDescriptor`,
  `TexturedMaterialDescriptor`, `GeometryDescriptor`), extended the same way
  when a test needs more than they take: texture slots by name, more meshes.
- **Texture and buffer descriptor files** come only from
  `DescribeTexture(leaf, row)` and `DescribeBuffer(relpath, row)`, which
  encode them with the product's `EncodeTextureResourceDescriptor` and
  `EncodeBufferResourceDescriptor` from the row's own descriptor.

## Invariants

A test cannot write content a cook never produces; it can only damage valid
content, as a [malformed artifact](#malformed-artifacts).

- **The encoder refuses what the reader refuses.** `EncodeLooseCookedIndex`
  checks every path with `RequireIndexRelPath`, gives every asset and texture
  descriptor a virtual path and a producing source, and refuses names on the
  records that may not carry them. A test that builds index records directly
  fails where it builds them, naming the record.
- **The kit fills what a cook fills.** Every asset and texture descriptor of a
  `LooseCookedRoot` records its producing source; every row has its payload's
  position; every descriptor's size and digest are those of its bytes, unless
  a test states otherwise (`LooseAsset::recorded_size`, `recorded_sha256`) to
  test a root that lies.
- **Pairs are built together.** A sidecar is built from its scene; a texture
  descriptor from its row; a pak from its loose roots.
- **A kit fix moves its tests.** When the kit gains what a test built by hand,
  every test that built it moves onto the kit in the same change, and the
  hand-written builder is deleted.

## Product seams

Serde routines write to and read from streams or devices; none takes or
returns a whole artifact as one buffer, so production never holds a file in
memory to write or read it.

1. **`PakFile` reads from any contiguous Serio device.** A path maps the file;
   a test hands it a `MemoryFile`. It parses its metadata in place and has no
   per-read streams.
2. **The Cooker reads and publishes through one port**, `CookerStorage`
   ([cooked sources](../cooked-sources/README.md)): the pak pipeline, the
   catalog IO, the cooked content view and summary, the import service's
   loose writer, and PakTool's artifacts, reports and staged sealing.
3. **The loose index encoder streams.**
   `data::loose_cooked::EncodeLooseCookedIndex(IndexContent, serio::AnyWriter&)`
   (`Data/LooseCookedIndexEncoder.h`) writes an index in one pass from its
   assets (`IndexedAsset`) and files (`IndexedFile`), in key and path order,
   so equal content encodes to equal bytes. `LooseCookedWriter::Finish`
   streams it into a publication of the port, and `LooseCookedRoot` streams it
   into a `MemoryStream`.

## Pipeline tests

The pak pipeline (`PakBuilder`, the planner, the writer, the manifest writer)
reads and publishes through the Cooker file port, so its tests run in memory:
a workspace over `MemoryFileSystem` holds the built sources, and what one
build publishes is the next build's source. Only subjects that still read the
disk directly are integration tests: `AssetLoader` mounting, the Player and
the executables. They get their inputs from the same builders and write them
out with the kit's `Materialize(files, folder)`, the one place the kit writes
files, under an `oxygen::testing::ScopedTempDir`.

A failure of a build prints every diagnostic (`HasNoErrors()`,
`HasDiagnostic(code)` matchers), so a failing test names its cause.

## Malformed artifacts

Reader tests need defects: a wrong version, a truncated catalog, a footer that
points outside the file, a pak without a browse index, a scene whose node table
runs past its end. `CookedBytes` and `DescriptorDefects` start from a valid
built artifact and apply one named defect, expressed against the format
structs:

```cpp
auto bytes = CookedPak(game).Encode();
CookedBytes::SetPakVersion(bytes, kCurrentPakFormatVersion + 1);
CookedBytes::MisalignTextureRegion(bytes);

auto scene = SceneBuilder { "Broken" };
scene.Node("root");
auto descriptor = scene.Bytes();
DescriptorDefects::PointNodeTablePastTheEnd(descriptor);
```

Each defect breaks exactly one rule, so a test of that rule fails for that
rule only. No test forges a whole artifact. A format's own reader tests (the
loose index reader's, the scene decoder's) may damage the bytes of their
format in place, starting from the product encoder's or a builder's output.

## Hardening

The builders are tested before any test uses them, in the kit's own unit
program, all in memory:

- Every built loose root loads through `LooseCookedIndex::LoadFromBytes` and
  the cooked summary reader, and every built pak opens through `PakFile` from
  a `MemoryFile`, with the entries, browse index, catalog and requirements the
  description implies.
- Each descriptor builder yields a descriptor its loader's parser accepts, with
  the records the description implies (a `SceneBuilder` scene through
  `SceneAsset`, a sidecar matching its scene).
- Each `CookedBytes` and `DescriptorDefects` defect produces the failure its
  reader names (`CookedOpenFailure`, the decoder's error), and nothing else.

One test pins the builders to the pipeline: the same content and identity
built by `CookedPak` and by `PakBuilder` over its loose root, in memory, give
the same bytes.

## What it replaces

| Hand-written today                                                                                                                                                                                                                                                                                                                                              | Replaced by                                       |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------- |
| Scene descriptors: `AssetLoader_{loading,scene,scripting,generation}_test` (`WriteLooseCookedScene*`, `SceneBytes`), `PakSceneTestSupport.h` (`BuildSceneDescriptorWithScriptingSlots` and its second builder), `ModelImportTestBase.h`, `ScenePipeline_functional_test`, `ScriptingSidecarImportJob_reimport`, the Inspector scene and geometry metadata tests | `SceneBuilder`, `LooseCookedRoot::Scene`          |
| Valid scenes in the scene decoder's tests (`SceneLoader_test`, `SceneLoader_composition_test`)                                                                                                                                                                                                                                                                  | `SceneBuilder`; their defects `DescriptorDefects` |
| A physics sidecar with bindings (`AssetLoader_physics_test`, `PhysicsLoaderTestFixtures.h`)                                                                                                                                                                                                                                                                     | `PhysicsSidecarBuilder`                           |
| Texture descriptor files forged in `MaterialDescriptorImportJob_functional_test` and `SceneDescriptorImportJob_functional_test`                                                                                                                                                                                                                                 | `AddTexture`, `DescribeTexture`                   |
| Hand-written records in model record tests                                                                                                                                                                                                                                                                                                                      | `ModelRecord`                                     |
| The Data tests' scene builder (`SceneAssetBuilder`) and its users: the scene composition, override and node reference tests, `AssetLoader_scene_query_test`, the Inspector scene metadata test, `SceneInstantiation_test`, `SceneScriptAttachment_test`                                                                                                         | `SceneBuilder`; their defects `DescriptorDefects` |
| Hand-laid scenes and sidecars of the DemoShell scene loader and environment settings tests                                                                                                                                                                                                                                                                      | `SceneBuilder`, `PhysicsSidecarDescriptor`        |

Replaced in steps 1-4: the tests' loose index writer and layout, the
second pak encoder, the interim pak kit, the texture root fixture, file-local
`WriteRoot`/`Material`/`MakeCatalog` helpers, `data::PakCatalog` literals
outside the catalog's own tests, the local index encoders of multi-mount
tests, and the hand-built physics sidecars without bindings.
`PakWriterTestSupport.h`'s hand-laid plans stay only in `PakWriter`'s own
tests, which are about laying out a given plan.

## Order

1. Done: `PakFile` over devices; the pak pipeline, the catalog IO, the cooked
   content view and the summary on the file port.
2. Done: the loose index encoder streams; `LooseCookedRoot` uses it.
3. Done: the kit target `Oxygen.Cooker.Testing` with `LooseCookedRoot`,
   `CookedPak`, `PakWorkspace`, `PackedPak`, `CookedBytes` and `Materialize`,
   and its own tests ([hardening](#hardening)).
4. Done: unit and functional tests of Content, Cooker, PakTool and Inspector
   on the kit; the encoder refuses what the reader refuses; multi-mount roots
   and scene-matched physics sidecars in the kit.
5. Done: integration tests (`AssetLoader` mounting, the Player, the
   examples) on the kit, their setup builder calls and `Materialize`.
6. Done: the descriptor builders (`SceneBuilder`, `PhysicsSidecarBuilder`,
   `DescriptorDefects`) and `ModelRecord`, with their hardening tests; every
   test in [what it replaces](#what-it-replaces) on them, its hand-written
   builder deleted.

## Decisions

- **Paks are packed by the product.** `CookedPak` runs the planner and the
  writer instead of laying out sections itself: a second layout of the pak
  format, however small, would be one more restatement to keep in step, and a
  pak no cook produces is a defect, which `CookedBytes` applies.
- **Builders compose product serde.** Every byte comes from a product routine
  or a format struct a builder fills from engine terms; a format change edits
  the product and that builder, never the tests.
- **Descriptors are built, not written.** A test describes a scene, a sidecar
  or a material in engine terms through its builder; no test lays out a
  descriptor's offsets, tables or strings.
- **Invariants live in the kit and the encoder.** A rule every cook follows is
  enforced once, where content is built, so a test cannot be written against
  content no cook produces.
- **Ports, not disk.** Code under test reads and publishes through the
  Cooker file port or Serio devices, so its tests run in memory; only code
  that still reads the disk directly gets integration tests, fed by the same
  builders through `Materialize`.
- **Serde streams.** An encoder writes to an output stream and a decoder
  reads from an input stream or a device; a whole-artifact buffer exists only
  where a caller chose a memory stream. `PakMeasureStore`'s section helpers
  still buffer one metadata section at a time for `PakWriter` (a table, the
  directory, the browse index; never payloads); the builders do not depend on
  it, and it is left as it is.
- **Builders never touch the disk.** Temp folders, file locks and leftovers
  of earlier runs can never fail a test that uses them; only `Materialize`,
  for integration tests, writes files.
- **Content is described in engine terms.** Mounts, assets, resources and
  references by virtual path; a builder never exposes a format field a test
  would have to keep in step.
- **Defects are applied to valid output.** A malformed-input test changes one
  thing about a valid artifact, so it keeps testing that thing after a format
  change.
