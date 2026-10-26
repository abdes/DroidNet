// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

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
    internal async Task<bool> FrameNodesAsync(IReadOnlyList<NodeReference> nodes)
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
        => _ = this.SetSelectionOutlineAsync(context.SelectedNodes, context.PrimaryNode);

    private async Task SetSelectionOutlineAsync(IReadOnlyList<NodeReference> nodes, NodeReference? activeNode)
    {
        try
        {
            if (!await this.services.Engine.SetSelectionOutlineAsync(nodes, activeNode).ConfigureAwait(true))
            {
                this.LogSelectionOutlineRejected();
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.services.OperationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = RuntimeOperationKinds.SelectionOutline,
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Warning,
                Title = "Selection outline failed",
                Message = "The runtime could not outline the selection.",
                AffectedScope = this.SceneScope(),
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }
    }
}
