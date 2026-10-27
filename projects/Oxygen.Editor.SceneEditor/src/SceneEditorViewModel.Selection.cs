// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Viewports;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// Viewport selection for the scene: applying the panes' picks to the shared selection, outlining
/// the selection in every editing view, and framing nodes requested by other panels.
/// </summary>
public partial class SceneEditorViewModel
{
    private const string ViewportSelectionSource = "Viewport";

    /// <summary>Frames nodes in the focused viewport; no nodes frame the whole scene.</summary>
    /// <param name="nodes">The nodes to frame.</param>
    /// <returns>Whether a viewport framed them.</returns>
    public async Task<bool> FrameNodesAsync(IReadOnlyList<NodeReference> nodes)
    {
        if (this.isDisposed || this.GetActiveViewport() is not { } viewport)
        {
            return false;
        }

        await viewport.FrameAsync(nodes).ConfigureAwait(true);
        return true;
    }

    private ISceneSelectionService SelectionService => this.services.Selection;

    private void AttachSelectionServices(ViewportViewModel viewport)
    {
        viewport.SelectionPicked = this.ApplyViewportSelection;
        viewport.SelectedNodesProvider = this.GetSelectedNodes;
    }

    private IReadOnlyList<NodeReference> GetSelectedNodes()
        => this.SelectionService.GetContext(this.Document.Id).SelectedNodes;

    private void ApplyViewportSelection(ViewportSelection selection)
    {
        if (this.isDisposed || selection.Combine(this.GetSelectedNodes()) is not { } nodes)
        {
            return;
        }

        // The Explorer reconciles its rows with the stored selection, reveals the active row and
        // tells the other panels.
        this.SelectionService.Publish(this.Document.Id, SceneSelectionContext.ForNodes(nodes), ViewportSelectionSource);
    }

    /// <summary>Outlines the document's selection in the editing views; only the active scene renders.</summary>
    private void UpdateSelectionOutline(SceneSelectionContext context)
        => _ = this.services.Viewports.OutlineSelectionAsync(this.Document.Id, context.SelectedNodes, context.PrimaryNode, this.SceneScope());
}
