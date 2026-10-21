# Oxygen.Editor.Shell.Testing

The test kit of `Oxygen.Editor.Shell`: what the tests of the parts that show
documents use instead of a window's document area. Only test projects reference
it.

- `TestDocumentArea` is an in-memory `IDocumentArea`: the test says which
  documents have a tab and which one is active, and `Activate` raises
  `ActiveDocumentChanged` as a tab switch does. Closing only removes the tab; a
  test of the close flow uses the Shell's own area.
