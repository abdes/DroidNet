# Loose cooked index reader

`LooseCookedIndex.Read(Stream)` reads the current native `container.index.bin`
into immutable document, asset and file-record metadata. Catalogs use it to
enumerate assets without launching a tool. The asset reference-block locator and
counts are retained as opaque metadata; native Data and Content remain the
authorities for validating and interpreting those blocks.

The reader checks section bounds, record sizes, required digests, member paths,
unique identities and paired resource-table/data roles. Texture descriptor
records carry the virtual path the texture is addressed by; asset and texture
virtual paths share one namespace. Every asset and texture descriptor records
the mount path of the source that produced it (`SourcePath`,
`/<Mount>/<path in mount>`), which is how the cooker tells a source re-cooking
its own asset from another source claiming the same identity. `TextureDescriptorFile` reads a texture descriptor (`.otex`) into
its `TextureResourceRecord`; the loose catalog lists each texture by its engine
key (`AssetKey.FromVirtualPath`) with that record. The reader accepts only the
current format; recook older content with current native tools.

An index is one commit of its root. `Retired` lists the files an earlier commit
listed and this one does not: they may still be there, and belong to no one
reading this commit. `Requirements` lists what the assets need of other mounts.
A resource data file records an all-zero digest, because each resource has its
own in its table, and may hold bytes past its committed length.
`IsReservedPath` names the root's own machinery, which is never content: the
index, the generation lock, the writer lock and the `.leases` folder.

The native format is owned by
[LooseCookedIndexFormat.h](../../../../Oxygen.Engine/src/Oxygen/Data/LooseCookedIndexFormat.h).
Complete payload verification is owned by
[native Content](../../../../Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md).
Reader tests use the `LooseCookedIndexFixture` emitter of the
[test kit](../../../testing/README.md); it is not included in the production
assembly.
