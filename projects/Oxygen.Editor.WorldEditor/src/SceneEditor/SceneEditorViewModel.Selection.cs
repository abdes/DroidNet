// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Viewport selection for the scene: applying the panes' picks to the shared selection, outlining
/// the selection in every editing view, and framing nodes requested by other panels.
/// </summary>
public partial class SceneEditorViewModel
{
    private const string ViewportSelectionSource = "Viewport";

    private ISceneSelectionService? SelectionService
        => this.container.Resolve<ISceneSelectionService>(IfUnresolved.ReturnDefault);

    private void AttachSelectionServices(ViewportViewModel viewport)
    {
        viewport.SelectionPicked = this.ApplyViewportSelection;
        viewport.SelectedNodesProvider = this.GetSelectedNodeIds;
    }

    private void RegisterSelectionMessages()
        => this.messenger.Register<FrameSceneNodesRequestMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.DocumentId == this.Metadata.DocumentId && this.GetActiveViewport() is { } viewport)
            {
                _ = viewport.FrameAsync(message.NodeIds);
            }
        });

    private IReadOnlyList<Guid> GetSelectedNodeIds()
        => this.SelectionService?.GetContext(this.Metadata.DocumentId).SelectedNodeIds ?? [];

    private void ApplyViewportSelection(ViewportSelection selection)
    {
        if (this.isDisposed || this.SelectionService is not { } service
            || selection.Combine(this.GetSelectedNodeIds()) is not { } nodeIds)
        {
            return;
        }

        // The Explorer reconciles its rows with the stored selection, reveals the active row and
        // tells the other panels.
        service.Publish(
            this.Metadata.DocumentId,
            nodeIds.Count == 0
                ? SceneSelectionContext.Empty
                : new SceneSelectionContext(SceneSelectionKind.Node, nodeIds, [], nodeIds[^1], PrimaryFolderId: null),
            ViewportSelectionSource);
    }

    /// <summary>Outlines the document's selection in the editing views; only the active scene renders.</summary>
    private void UpdateSelectionOutline(SceneSelectionContext context)
        => _ = this.SetSelectionOutlineAsync(context.SelectedNodeIds, context.PrimaryNodeId);

    private async Task SetSelectionOutlineAsync(IReadOnlyList<Guid> nodeIds, Guid? activeNodeId)
    {
        try
        {
            if (!await this.engineService.SetSelectionOutlineAsync(nodeIds, activeNodeId).ConfigureAwait(true))
            {
                this.LogSelectionOutlineRejected();
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.operationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = RuntimeOperationKinds.SelectionOutline,
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Warning,
                Title = "Selection outline failed",
                Message = "The runtime could not outline the selection.",
                AffectedScope = new AffectedScope { DocumentId = this.Metadata.DocumentId, DocumentName = this.Metadata.Title },
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }
    }
}
