# Deferred Items

## 2026-04-17

- Out-of-scope CTest registration noise: the broad plan regex
  `ShadowService|SceneRendererDeferredCore|DrawMetadataEmitter` also matches
  two pre-existing `Oxygen.Renderer.DrawMetadataEmitter` entries
  (`Tests_NOT_BUILT` and `Tests`) whose executables are absent in this build
  tree. Vortex in-scope tests passed; the missing renderer executables were not
  introduced by `04-03` and were not modified here.
