# Oxygen.Editor.Assets.Testing

The test kit of `Oxygen.Editor.Assets`: the asset entries the feed publishes for
common assets, which the tests of the parts that show assets (the Content
Browser, the inspectors, the material editor) give their feeds instead of
building `AssetEntry` by hand in each test. Only test projects reference it.

- `TestAssetEntries.CookedMaterial(index)` is a material of the Content mount
  whose cooked output is current.
- `TestAssetEntries.ImportedModel(imported)` is a model source before or after
  its import; `ImportedOutputs(model)` are the entries of its geometry, material
  and scene outputs, which name the model as their import source.

A feed over these entries is a one-off answer: tests set up `IAssetFeed.Items`
with Moq.
