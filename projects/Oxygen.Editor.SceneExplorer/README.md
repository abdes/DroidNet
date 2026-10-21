# Oxygen.Editor.SceneExplorer

The Scene Explorer panel: the tree of the active scene document's nodes and
explorer folders, where nodes are created, renamed, moved, grouped, hidden,
locked and selected.

The explorer edits through the scene document's authoring and follows the
selection every scene part shares (Scenes' `ISceneSelectionService`). It
reaches the scene views (framing) through Scenes' `ISceneViews` and contributes
its commands (rename, find) to the Shell's command service.
