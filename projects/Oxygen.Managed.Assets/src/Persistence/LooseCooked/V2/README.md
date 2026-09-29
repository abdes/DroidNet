# Loose cooked index reader

`LooseCookedIndex.Read(Stream)` reads the current native v2
`container.index.bin` into immutable `Document`, `AssetEntry` and `FileRecord`
models. Catalogs use this metadata to enumerate assets without running a tool.

The reader checks section bounds, record sizes, required digests, member paths,
unique identities and paired resource-table/data roles. Auxiliary file records
may repeat their role, with unique physical paths. `AssetKey.ToString()` uses the
same canonical UUID text as the native Inspector.

The native format is owned by
[LooseCookedIndexFormat.h](../../../../../Oxygen.Engine/src/Oxygen/Data/LooseCookedIndexFormat.h).
Complete payload verification is owned by
[native Content](../../../../../Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md).
This managed reader does not hash payloads, write cooked indexes or accept older
formats. Reimport or recook old content with the current native tools.

Reader tests use the separate `testsupport/LooseCookedIndexFixture.cs` emitter;
it is not included in the production assembly.
