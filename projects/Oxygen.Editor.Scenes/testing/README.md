# Oxygen.Editor.Scenes.Testing

The test kit of `Oxygen.Editor.Scenes`: what other modules' tests use to open
scene documents and stand in for the scene ports, instead of building scene
kinds, file stores and projects by hand in each test. Only test projects
reference it.

- `TestSceneDocuments` opens scene documents with the real scene kind over the
  files of a `TestProject`, read and written by the real atomic store on the
  project's file system (Testably's `MockFileSystem` in unit tests). `Write`
  puts a scene in its file as another program would, `FileOf` names a scene's
  file, and `OpenAsync` opens it. `BeforeRead` holds a read until the test
  releases it, and `otherKinds` opens other documents, such as materials, beside
  the scenes.
- `TestScenes.Open(scene)` writes a scene a test built to a file of an
  in-memory project and opens it, as the editor opens it. The `TestScene` it
  returns holds the document and the document's own scene and nodes (`NodeOf`);
  disposing it closes them.
- `FakeNodeInteraction` is an in-memory `ISceneNodeInteraction`: the lock and
  hide state a test sets, with descendants locked through a locked ancestor.

Fakes of the pipeline ports a scene document takes come from
`Oxygen.Editor.ContentPipeline.Testing`.
