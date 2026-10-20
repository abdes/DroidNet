# Oxygen.Editor.Documents

What the editor edits, independent of the window that shows it: documents, the
files they read and write, and the kinds that decode them. Where documents show
and how they close is the [Shell](../Oxygen.Editor.Shell/README.md)'s.

Design: [document-kinds.md](../../design/editor/review/document-kinds.md) and
[document-identity.md](../../design/editor/review/document-identity.md).

## Documents and document kinds

What a document is, independent of where it shows, is in `Document`,
`OpenDocuments` and the kind contract
([document-kinds.md](../../design/editor/review/document-kinds.md)). None of it
knows windows or tabs.

- A `Document` edits one file. It holds the `FileVersion` it read or last saved,
  and every save writes only over that version, through `IAtomicFileStore`, so
  an outside change surfaces as a conflict. It is dirty while its `History` is
  away from the position of its last save or load, so undoing back to that
  position makes it clean. When told its file changed (`CheckFileAsync`), it
  ignores its own save, reloads quietly when it has no edits, and otherwise
  marks itself changed on disk or missing. It also reloads, keeps its edits over
  an outside change (`KeepMineAsync`), saves a copy, and follows a relocation
  without a history entry.
- `OpenDocuments` opens each file once, with the `IDocumentKind` registered for
  its suffix (the longest suffix wins, and no two kinds share one).
- An `IDocumentKind` only decodes a file into its `IDocumentContent`. The
  content finishes its edits in progress, captures what a save writes (or
  refuses with a reason), and applies a relocation to its model. A kind never
  reads or writes a file and holds no version or history.

```csharp
var documents = new OpenDocuments([materialKind], atomicFileStore, loggerFactory);
var opened = await documents.OpenAsync(new DocumentFile(path, assetUri));
if (opened.Document is { } document)
{
    // Editors record their edits into document.History.
    var saved = await document.SaveAsync();
}
```

## Ports

Two ports give documents what the project knows, without a reference to the
modules that know it; ContentPipeline implements both.

- `IDocumentFiles` finds the file an authored asset is edited in, and the
  document file at a path inside the project's content folders.
- `IDocumentRelocations` follows the relocations the editor makes: what each open
  document follows, before the relocation completes, and the files a change
  deleted.

## Identity

A document has one id, its `Document.Id`, a `DocumentId` from
[DroidNet.Documents](../Documents/README.md). The tab that shows it is keyed by
the same id, and `OpenDocuments` finds a document by id (`Find(DocumentId)`), by
file (`Find(path)`) or by asset (`FindByAsset(Uri)`). Persisted view state is
keyed by what the document edits, never by `DocumentId`, which lives for one
session.

## Dependencies

- [DroidNet.Documents](../Documents/README.md): `DocumentId`.
- `DroidNet.Storage`: `IAtomicFileStore`, through which every file is read and
  written.
- `Microsoft.Extensions.Logging`.
