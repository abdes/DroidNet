# Oxygen.Editor.Cooking

The Cooking panel: the cook runs of the project, with their issues, output and
imported assets.

`CookingModule` contributes the panel (`cook`) to each workspace window.
`CookingPresenter` brings it forward when explicit work asks for it.
`CookRecovery` is what the panel does around a cook:

- It saves the unsaved documents a cook reads, after asking
  (`IDocumentService.SaveBeforeAsync` over `IUnsavedCookInputs`).
- A scene property or script slot an issue names is opened through placement
  and shown through the scene selection (`ISceneSelectionService`), which also
  carries the request to focus the property.
- It asks the cooked inspection to inspect a run's scope (`ICookedInspections`)
  and the Content Explorer to show a run's imported assets (`IAssetReveal`).
  Both are Shell ports, so the panel references neither feature.

The panel presents cook runs only; orchestrating them belongs to
ContentPipeline.
