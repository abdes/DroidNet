# Loose cooked index reader

`LooseCookedIndex.Read(Stream)` reads the current native `container.index.bin`
into immutable document, asset and file-record metadata. Catalogs use it to
enumerate assets without launching a tool. The asset reference-block locator and
counts are retained as opaque metadata; native Data and Content remain the
authorities for validating and interpreting those blocks.

The reader checks section bounds, record sizes, required digests, member paths,
unique identities and paired resource-table/data roles. Texture descriptor
records carry the virtual path the texture is addressed by; asset and texture
virtual paths share one namespace. `TextureDescriptorFile` reads a texture
descriptor (`.otex`) into its `TextureResourceRecord`; the loose catalog lists
each texture by its engine key (`AssetKey.FromVirtualPath`) with that record.
The reader accepts only the current format; recook older content with current
native tools.

The native format is owned by
[LooseCookedIndexFormat.h](../../../../Oxygen.Engine/src/Oxygen/Data/LooseCookedIndexFormat.h).
Complete payload verification is owned by
[native Content](../../../../Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md).
Reader tests use the separate `testsupport/LooseCookedIndexFixture.cs` emitter;
it is not included in the production assembly.
