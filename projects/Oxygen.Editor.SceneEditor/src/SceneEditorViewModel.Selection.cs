// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Managed.Core;
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
    /// <param name="nodeIds">The nodes to frame.</param>
    /// <returns>Whether a viewport framed them.</returns>
    internal async Task<bool> FrameNodesAsync(IReadOnlyList<NodeId> nodeIds)
    {
        if (this.isDisposed || this.GetActiveViewport() is not { } viewport)
        {
            return false;
        }

        await viewport.FrameAsync(nodeIds).ConfigureAwait(true);
        return true;
    }

    private ISceneSelectionService SelectionService => this.services.Selection;

    private void AttachSelectionServices(ViewportViewModel viewport)
    {
        viewport.SelectionPicked = this.ApplyViewportSelection;
        viewport.SelectedNodesProvider = this.GetSelectedNodeIds;
    }

    private IReadOnlyList<NodeId> GetSelectedNodeIds()
        => this.SelectionService.GetContext(this.Document.Id).SelectedNodeIds;

    private void ApplyViewportSelection(ViewportSelection selection)
    {
        if (this.isDisposed || selection.Combine(this.GetSelectedNodeIds()) is not { } nodeIds)
        {
            return;
        }

        // The Explorer reconciles its rows with the stored selection, reveals the active row and
        // tells the other panels.
        this.SelectionService.Publish(
            this.Document.Id,
            nodeIds.Count == 0
                ? SceneSelectionContext.Empty
                : new SceneSelectionContext(SceneSelectionKind.Node, nodeIds, [], nodeIds[^1], PrimaryFolderId: null),
            ViewportSelectionSource);
    }

    /// <summary>Outlines the document's selection in the editing views; only the active scene renders.</summary>
    private void UpdateSelectionOutline(SceneSelectionContext context)
        => _ = this.SetSelectionOutlineAsync(context.SelectedNodeIds, context.PrimaryNodeId);

    private async Task SetSelectionOutlineAsync(IReadOnlyList<NodeId> nodeIds, NodeId? activeNodeId)
    {
        try
        {
            if (!await this.services.Engine.SetSelectionOutlineAsync(nodeIds, activeNodeId).ConfigureAwait(true))
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
