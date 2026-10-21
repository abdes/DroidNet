# Oxygen.Managed.Assets.Testing

The test kit of `Oxygen.Managed.Assets`: small cooked containers for the tests
of the modules that read cooked content, written in the engine's formats instead
of cooked by the native tools. Only test projects reference it.

- `LooseCookedIndexFixture.Write` writes a loose cooked index for a `Document`;
  `RewriteFormatVersionAsync` makes an index on disk claim another format
  version.
- `PakFixture.Write` writes a pak for a `PakDocument`, with an optional browse
  index and texture table; `Encode` lays out a texture table record.
