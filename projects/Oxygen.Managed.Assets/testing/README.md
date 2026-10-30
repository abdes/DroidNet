# Oxygen.Managed.Assets.Testing

The test kit of `Oxygen.Managed.Assets`: small cooked containers for the tests
of the modules that read cooked content, written in the engine's formats instead
of cooked by the native tools. Only test projects reference it.

- `LooseCookedIndexFixture.Write` writes a loose cooked index for a `Document`;
  `RewriteFormatVersionAsync` makes an index on disk claim another format
  version.
- `PakFixture.Write` writes a pak for a `PakDocument`, with an optional browse
  index and texture table; `Encode` lays out a texture table record.
- `AuthoredFiles` is the one writer of each authored format the editor's tests
  read: `Material`, `Texture`, `Geometry` and `Input` start a document
  (`MaterialDocument`, `TextureDocument`, `GeometryDocument`, `InputDocument`)
  from the minimal text its schema accepts, with the document's identity.
  `Set`, `Remove` and `Append` change any other field by JSON pointer; `Text`
  and `Bytes` give the file's content. A reference field is written with
  `Reference` or a named member (`MaterialDocument.Texture`,
  `InputDocument.ActionReference`) and the target's identity, never as text.

The rule: one writer per format, and `AuthoredFiles.SpellReference` is the only
place that spells a reference. A test says what a document must contain; when
the reference contract changes, that method and the writers change, not the
tests. A test whose subject is a spelling (a reader refusing malformed text)
writes that one text with `Set`, or keeps the whole malformed text inline.
