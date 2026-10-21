# Oxygen.Editor.Problems

The Problems panel: the problems of every asset family of the project (script
analysis, input sources) and of the running scene, grouped by file, with the
full diagnostic of the selected one.

`ProblemsModule` contributes the panel (`problems`) to each workspace window.
From the window's start `ProblemsPresenter` keeps the tab title carrying the
error and warning counts (`IPlacementService.SetPanelTitle`), brings the panel
forward when the running scene reports an error, and answers
`ShowProblemMessage` (sent by a script slot's badge) by selecting the problem.
`RuntimeProblemNodes` shows the scene document a runtime problem names and
selects its node through `ISceneSelectionService`.

The panel presents problems only; producing them belongs to the family modules
and to LivePreview.
