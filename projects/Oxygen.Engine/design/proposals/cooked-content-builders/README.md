# Cooked content builders

Status: proposal

Last updated: 2026-10-30

Engine tests need cooked content: loose cooked roots, paks, their catalogs and
browse indexes, pak build requests and model import records. This proposal
defines one builder per artifact, composed from the product's own serde
routines, so a format change edits the product, never the tests. The builders
work in memory: they never write or read a file.

## Contents

- [Problem](#problem)
- [Model](#model)
- [Builders](#builders)
- [Product seams](#product-seams)
- [Pipeline tests](#pipeline-tests)
- [Malformed artifacts](#malformed-artifacts)
- [Hardening](#hardening)
- [What it replaces](#what-it-replaces)
- [Order](#order)
- [Decisions](#decisions)

## Problem

Today a test that needs a pak or a loose root writes one by hand: forged
descriptor bytes through `LooseCookedTestWriter`, a pak through `PakTestWriter`
(a second encoder of the pak format), `data::PakCatalog` literals, request
structs filled field by field, and a dozen file-local `WriteRoot`/`Material`
helpers. Each restates the format. When the format changes (catalog version 4,
a mandatory browse index, one library per pak), every restatement breaks
separately, and each one is fixed by hand.

## Model

A builder describes cooked content the way the engine defines it, never the
way a test happens to need it:

| Concept      | Engine definition                                                                                    | Builder term                                                                                              |
| ------------ | ---------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| Library      | One mount root; a loose root or a pak holds one                                                      | `CookedLibrary("Props")`; every path it takes is relative to `/Props`                                     |
| Asset        | A typed descriptor at a virtual path; its key derives from the path                                  | `.Material("Materials/Red.omat")`, `.Geometry(...)`, `.Scene(...)`, one method per `AssetType`            |
| Resource     | A texture, buffer, script or physics resource in the library's tables                                | `.Texture("Textures/Wood.otex", { .width = 4 })`, `.Buffer(...)`, `.Script(...)`, `.PhysicsResource(...)` |
| Reference    | An asset's key reference to an asset or texture, possibly in another library                         | `.References("/Engine/Default.omat")`, `.UsesTexture("/Engine/Grid.otex")`, by virtual path               |
| Deletion     | A pak's tombstone of a lower asset                                                                   | `.Deletes("/Props/Old.omat")`                                                                             |
| Pak          | A library with its catalog, browse index and requirements, over dependencies and, for a patch, bases | `CookedPak` of a `CookedLibrary`, `.DependsOn(engine)`, `.PatchOf(base)`                                  |
| Model record | `<model identity>.import.json` in the model's mount                                                  | `ModelRecord("/Props/Models/Truck.glb").Source("Truck.fbx")`                                              |

A test names only what it is about: a material with one texture, a scene with
two nodes. Every other field has the value a cook writes.

## Builders

All builders live in one engine test kit, `Oxygen.Cooker.Testing`
(`src/Oxygen/Cooker/testing/`), which any engine test program links: Content,
Cooker, PakTool, Inspector, Player and the examples' tests. Their output is a
`MemoryFileSystem` (the existing test port over `ILibraryFiles`) or bytes.

| Builder         | Produces                                                                | From the product                                                                                                               |
| --------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `CookedLibrary` | A loose cooked root: index, descriptors, resource tables and data files | Descriptor structs and `MakeDescriptorBytes`, `AssetReferences::Encode`, resource descriptor structs, `EncodeLooseCookedIndex` |
| `CookedPak`     | The bytes of a pak of one library                                       | Header and footer structs, `PakMeasureStore` (tables, directory, browse index), `PakCatalog::Encode`, the CRC routine          |
| `PakRequest`    | A `PakBuildRequest`: full or patch, sources, dependencies and bases     | The request types; dependencies and bases read through `PakFile` from built paks                                               |
| `ModelRecord`   | A model import record at its identity                                   | The record's serializer                                                                                                        |
| `CookedBytes`   | A named defect applied to a built artifact's bytes                      | The format structs (`PakHeader`, `PakFooter`, the catalog and index headers)                                                   |

Every byte a builder emits comes from a product routine. The only thing
`CookedPak` decides is where each section goes, and it follows the rules
`PakFile` checks (alignment, no overlap, the catalog before the footer); the
kit's own tests open every built pak with the real `PakFile`.

`CookedPak` takes the requirements a test describes: a pak that references
`/Engine/Default.omat` requires it of `Engine`. Whether the planner resolves
references into exactly those requirements is the planner's own test, a
[pipeline test](#pipeline-tests).

## Product seams

Two contained changes let the builders use product serde and let unit tests
read what they build. Serde routines write to and read from streams; none
takes or returns a whole artifact as one buffer, so production never holds a
file in memory to write or read it.

1. **The loose index encoder streams.** Today `LooseCookedWriter::Finish`
   serializes the whole index into a `serio::MemoryStream` and publishes it
   with `WriteFileAtomically(path, bytes)`, because Serio's atomic
   publication only takes a complete buffer: a 1 GB index costs 1 GB of
   memory. The serialization becomes
   `EncodeLooseCookedIndex(..., serio::AnyOutputStream&)`, writing through a
   `serio::Writer`. Serio gains `AtomicFileWriter`: it opens the unique
   sibling file as an output stream, and `Commit()` flushes it and replaces
   the destination, with the durability and sharing rules of
   `WriteFileAtomically`. `Finish` streams the index into it; the builders
   stream it into a `MemoryStream`.
2. **`PakFile` reads from a path or from memory.** Its parsing (header,
   footer, directory, catalog, browse index, asset references) uses only
   stream operations, but is typed to `serio::FileStream<>`: the constructor
   opens five file streams by path, the `Reader` alias returned by
   `CreateReader` and the `Create*DataReader` accessors names `FileStream`,
   and `ValidateCrc32Integrity` re-reads the file by path. Its parsing takes
   the input-stream abstraction, and `Reader` becomes the type-erased
   `serio::AnyReader`; the parsing code itself does not change. The path
   constructor keeps streaming from the file; a second constructor reads an
   in-memory pak through a `ReadOnlyMemoryStream`, for tests and tools that
   already hold one.

The runtime source `PakFileSource` keeps opening the pak by path for each
load through `ContentFileReader`, so concurrent loads hold independent
handles; mounting through `AssetLoader` stays disk-based.

## Pipeline tests

Some subjects are pipelines that read and write disk by design: `PakBuilder`
(planner and writer), the import service, `AssetLoader` mounting, the Player
and the PakTool executable. Their tests are integration tests. They get their
inputs from the same builders and write them out with the kit's
`Materialize(files, folder)`, the one place the kit writes files; their
expectations are stated in builder terms too (the requirements a built
`CookedPak` holds are compared with the requirements a planner-built pak
holds).

## Malformed artifacts

Reader tests need defects: a wrong version, a truncated catalog, a footer that
points outside the file, a pak without a browse index. `CookedBytes` starts
from a valid built artifact and applies one named defect, expressed against
the format structs:

```cpp
auto bytes = CookedPak(library).Encode();
CookedBytes::SetPakVersion(bytes, kCurrentPakFormatVersion + 1);
CookedBytes::DropBrowseIndex(bytes);
```

Each defect breaks exactly one rule, so a test of that rule fails for that
rule only. No test forges a whole artifact.

## Hardening

The builders are tested before any test uses them, in the kit's own unit
program, all in memory:

- Every built library loads through `LooseCookedIndex::LoadFromBytes` and
  `ReadLibraryFacts`, and every built pak opens through `PakFile` from bytes,
  with the entries, browse index, catalog and requirements the description
  implies.
- Each asset method yields a descriptor its loader's parser accepts.
- Each `CookedBytes` defect produces the failure its reader names
  (`CookedOpenFailure`), and nothing else.

One integration test pins the builders to the pipeline: the same library
built by `CookedPak` and by `PakBuilder` over its materialized loose root
opens to the same catalog, browse index and directory.

## What it replaces

| Today                                                                              | Becomes                        |
| ---------------------------------------------------------------------------------- | ------------------------------ |
| `Content/test/Fixtures/LooseCookedTestWriter.h`, `LooseCookedTestLayout.h`         | `CookedLibrary`, `CookedBytes` |
| `Content/test/Fixtures/PakTestWriter.h` (a second pak encoder)                     | `CookedPak`, `CookedBytes`     |
| `Cooker/test/Pak/PakTestSupport.h` requests, base catalogs, loose roots            | `PakRequest`, `CookedLibrary`  |
| `Cooker/test/Support/TextureLibraryFixture.h`                                      | `CookedLibrary`, `CookedPak`   |
| File-local `WriteRoot`, `Material`, `Bindings`, `MakeCatalog`, `MakeEntry` helpers | The builders                   |
| `data::PakCatalog` literals in tests other than the catalog's own                  | `CookedPak`                    |
| Hand-written records in model record tests                                         | `ModelRecord`                  |

`PakWriterTestSupport.h`'s hand-laid plans stay only in `PakWriter`'s own
tests, which are about laying out a given plan.

## Order

1. The two product seams, with their tests.
2. The kit and its own tests ([hardening](#hardening)).
3. Unit and functional tests move to the kit; the replaced writers and helpers
   are deleted.
4. Integration tests move to the kit in the integration slice, by replacing
   their setup with builder calls and `Materialize`.

## Decisions

- **Builders compose product serde.** Every byte comes from a product routine;
  a builder never encodes a format itself, so a format change edits the
  product only.
- **Two seams, no IO rewrite.** The loose index encoder writes to a stream
  and `PakFile` reads from one; the cooker's pipelines keep their disk IO, and
  their tests are integration tests fed by the same builders.
- **Serde streams.** An encoder writes to an output stream and a decoder
  reads from an input stream; a whole-artifact buffer exists only where a
  caller chose a memory stream. `PakMeasureStore`'s section helpers still
  buffer one metadata section at a time for `PakWriter` (a table, the
  directory, the browse index; never payloads); the builders do not depend on
  it, and it is left as it is.
- **Builders never touch the disk.** Temp folders, file locks and leftovers
  of earlier runs can never fail a test that uses them; only `Materialize`,
  for pipeline tests, writes files.
- **Content is described in engine terms.** Libraries, assets, resources and
  references by virtual path; a builder never exposes a format field a test
  would have to keep in step.
- **Defects are applied to valid output.** A malformed-input test changes one
  thing about a valid artifact, so it keeps testing that thing after a format
  change.
