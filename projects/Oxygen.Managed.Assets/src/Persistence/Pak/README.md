# Pak reader

`PakIndex.Read(Stream)` reads a pak file's browsable metadata: the header's
source identity and content version, the asset directory and the embedded browse
index. It returns one entry per asset the browse index names, joined with its
directory entry (type, descriptor location and reference-block locator), so
catalogs can list a pak's assets by virtual path without launching a tool.
`PakIndex.ReadFileAsync(FileSystemPath)` reads a pak file the same way through
its own handle, shared with other readers only; callers that must keep the pak
unchanged while they use it hold their own handle and call `Read`.

The reader checks the header and footer magic, the format and browse index
versions, the UUIDv7 source identity, the directory and browse index bounds,
duplicate keys and paths, browse entries naming assets the directory lacks, and
texture entries outside the texture table. Asset and texture virtual paths share
one namespace. A pak without a browse index is rejected: the editor lists pak
assets by virtual path. The reader accepts only the current format; repack older
content with current native tools.

The native format is owned by
[PakFormat_core.h](../../../../Oxygen.Engine/src/Oxygen/Data/PakFormat_core.h);
loading, payload verification and the CRC are owned by native Content's
`PakFile`. Reader tests use the separate `testsupport/PakFixture.cs` emitter; it
is not included in the production assembly.
