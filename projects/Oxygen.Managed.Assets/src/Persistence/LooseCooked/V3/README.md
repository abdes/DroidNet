# Loose cooked index reader

`LooseCookedIndex.Read(Stream)` reads the current native v3 `container.index.bin`
into immutable document, asset and file-record metadata. Catalogs use it to
enumerate assets without launching a tool. The asset reference-block locator and
counts are retained as opaque metadata; native Data and Content remain the
authorities for validating and interpreting those blocks.

The reader checks section bounds, record sizes, required digests, member paths,
unique identities and paired resource-table/data roles. V3 has no script-binding
file roles because scene script slots are descriptor-local. The reader accepts
only the current format; recook older content with current native tools.

The native format is owned by
[LooseCookedIndexFormat.h](../../../../../Oxygen.Engine/src/Oxygen/Data/LooseCookedIndexFormat.h).
Complete payload verification is owned by
[native Content](../../../../../Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md).
Reader tests use the separate `testsupport/LooseCookedIndexFixture.cs` emitter;
it is not included in the production assembly.
