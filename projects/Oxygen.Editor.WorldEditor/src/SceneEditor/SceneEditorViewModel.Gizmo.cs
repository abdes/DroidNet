// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The transform gizmo of the scene's selection: which nodes it moves, and applying its drags
/// through the authoring commands so a drag previews live, commits one undo entry and cancels
/// exactly.
/// </summary>
/// <remarks>
/// The engine hit tests and drags the gizmo and reports each drag's new local transforms; this
/// view model is the only writer of the authored scene. An Alt-drag previews on the originals and,
/// on release, restores them and duplicates them at the dragged transforms, so a cancelled Alt-drag
/// leaves no copy and no history.
/// </remarks>
public partial class SceneEditorViewModel
{
    private const string GizmoSessionKey = "Viewport.Transform";

    private SynchronizationContext? gizmoContext;
    private GizmoTransaction? gizmoTransaction;

    /// <summary>Gets the viewport tools shared by the scene's panes.</summary>
    public TransformToolsViewModel TransformTools { get; private set; } = null!;

    /// <summary>Builds the property edits that give each target its dragged transform.</summary>
    /// <param name="tool">The dragged gizmo's tool: it decides which properties the drag changes.</param>
    /// <param name="targets">The targets' new local transforms.</param>
    /// <returns>One edit per target.</returns>
    internal static Dictionary<Guid, PropertyEdit> BuildGizmoEdits(RuntimeTransformTool tool, IReadOnlyList<RuntimeGizmoTarget> targets)
    {
        var transform = SceneDocumentCommandService.Transform;
        var edits = new Dictionary<Guid, PropertyEdit>(targets.Count);
        foreach (var target in targets)
        {
            // Every drag moves positions: rotation and scale act about the pivot.
            var edit = new PropertyEdit();
            edit.Set(transform.PositionX, target.Position.X);
            edit.Set(transform.PositionY, target.Position.Y);
            edit.Set(transform.PositionZ, target.Position.Z);
            if (tool == RuntimeTransformTool.Rotate)
            {
                var euler = TransformConverter.QuaternionToEulerDegrees(target.Rotation);
                edit.Set(transform.RotationX, euler.X);
                edit.Set(transform.RotationY, euler.Y);
                edit.Set(transform.RotationZ, euler.Z);
            }
            else if (tool == RuntimeTransformTool.Scale)
            {
                edit.Set(transform.ScaleX, target.Scale.X);
                edit.Set(transform.ScaleY, target.Scale.Y);
                edit.Set(transform.ScaleZ, target.Scale.Z);
            }

            edits[target.NodeId] = edit;
        }

        return edits;
    }

    /// <summary>Applies one gizmo interaction reported by the engine; runs on the UI thread.</summary>
    /// <param name="gizmoEvent">The interaction.</param>
    internal void ApplyGizmoEvent(RuntimeGizmoEvent gizmoEvent)
    {
        if (this.isDisposed)
        {
            return;
        }

        var viewport = this.Viewports.FirstOrDefault(pane => pane.AssignedViewId == gizmoEvent.ViewId);
        switch (gizmoEvent.Kind)
        {
            case RuntimeGizmoEventKind.Hover:
                viewport?.SetGizmoHover(gizmoEvent.Hovering);
                break;
            case RuntimeGizmoEventKind.Begin when viewport is not null && this.scene is not null:
                this.gizmoTransaction = new GizmoTransaction(viewport, gizmoEvent.Tool, gizmoEvent.Duplicate, this.CreateCommandContext());
                viewport.BeginGizmoDrag();
                break;
            case RuntimeGizmoEventKind.Update when this.gizmoTransaction is { } transaction && ReferenceEquals(transaction.Viewport, viewport):
                transaction.Viewport.UpdateGizmoDrag(gizmoEvent);
                if (gizmoEvent.Representable)
                {
                    this.QueueGizmoTargets(transaction, gizmoEvent.Targets);
                }

                break;
            case RuntimeGizmoEventKind.Commit or RuntimeGizmoEventKind.Cancel
                when this.gizmoTransaction is { } transaction && ReferenceEquals(transaction.Viewport, viewport):
                this.gizmoTransaction = null;
                transaction.Viewport.EndGizmoDrag();
                transaction.Completion = this.CompleteGizmoTransactionAsync(
                    transaction,
                    gizmoEvent.Kind == RuntimeGizmoEventKind.Commit,
                    gizmoEvent.Representable ? gizmoEvent.Targets : null);
                break;
            default:
                break;
        }
    }

    /// <summary>The nodes the gizmo moves: the selection without locked nodes, and without nodes whose ancestor is selected too.</summary>
    /// <param name="activeNodeId">The node whose origin is the pivot.</param>
    /// <returns>The targets, in selection order.</returns>
    internal IReadOnlyList<Guid> GetGizmoTargets(out Guid? activeNodeId)
    {
        activeNodeId = null;
        if (this.scene is null || this.SelectionService is not { } selection)
        {
            return [];
        }

        var context = selection.GetContext(this.Metadata.DocumentId);
        var nodes = this.FindSelectionRoots(context.SelectedNodeIds);
        var interaction = this.Interaction;
        var targets = nodes.Where(node => interaction?.GetLockOwner(node) is null).Select(static node => node.Id).ToList();
        activeNodeId = context.PrimaryNodeId is { } primary && targets.Contains(primary) ? primary
            : targets.Count > 0 ? targets[^1] : null;
        return targets;
    }

    private static string GizmoLabel(RuntimeTransformTool tool, int count)
    {
        var verb = tool switch
        {
            RuntimeTransformTool.Rotate => "Rotate",
            RuntimeTransformTool.Scale => "Scale",
            _ => "Move",
        };
        return count == 1 ? verb : $"{verb} {count} nodes";
    }

    private void InitializeTransformTools()
    {
        this.gizmoContext = SynchronizationContext.Current;
        var settings = this.container.Resolve<TransformToolSettingsService>(IfUnresolved.ReturnDefault)
            ?? new TransformToolSettingsService(settings: null);
        this.TransformTools = new TransformToolsViewModel(settings);
        this.TransformTools.Changed += this.OnTransformToolsChanged;
        this.engineService.GizmoEvent += this.OnEngineGizmoEvent;
        if (this.Interaction is { } interaction)
        {
            interaction.StateChanged += this.OnInteractionStateChanged;
        }

        _ = settings.EnsureLoadedAsync();
    }

    private void DisposeTransformTools()
    {
        this.engineService.GizmoEvent -= this.OnEngineGizmoEvent;
        if (this.Interaction is { } interaction)
        {
            interaction.StateChanged -= this.OnInteractionStateChanged;
        }

        this.TransformTools.Changed -= this.OnTransformToolsChanged;
        this.TransformTools.Dispose();
    }

    private void AttachGizmoServices(ViewportViewModel viewport)
    {
        viewport.TransformTools = this.TransformTools;
        viewport.EditCommandRequested = this.RunViewportEditAsync;
        viewport.DisplayScaleChanged = this.UpdateTransformGizmo;
    }

    private void OnTransformToolsChanged(object? sender, EventArgs e) => this.UpdateTransformGizmo();

    private void OnInteractionStateChanged(object? sender, EventArgs e) => this.UpdateTransformGizmo();

    // Gizmo events arrive on a worker thread, in order; they are applied in order on the UI thread.
    private void OnEngineGizmoEvent(object? sender, RuntimeGizmoEventArgs e)
    {
        if (this.gizmoContext is { } context)
        {
            context.Post(_ => this.ApplyGizmoEvent(e.Event), state: null);
        }
        else
        {
            this.ApplyGizmoEvent(e.Event);
        }
    }

    /// <summary>Shows the gizmo of the current selection; only the active scene renders.</summary>
    private void UpdateTransformGizmo()
    {
        if (this.isDisposed || this.scene is null || !this.IsActiveDocument())
        {
            return;
        }

        var targets = this.GetGizmoTargets(out var activeNodeId);
        var settings = this.TransformTools.Settings;
        var gizmo = new RuntimeTransformGizmo(
            this.TransformTools.Tool,
            settings.Space,
            settings.Snap,
            targets,
            activeNodeId,
            (float)(this.GetActiveViewport()?.DisplayScale ?? 1.0));
        _ = this.SetTransformGizmoAsync(gizmo);
    }

    private async Task SetTransformGizmoAsync(RuntimeTransformGizmo gizmo)
    {
        try
        {
            if (!await this.engineService.SetTransformGizmoAsync(gizmo).ConfigureAwait(true))
            {
                this.LogTransformGizmoRejected();
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.operationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = RuntimeOperationKinds.TransformGizmo,
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Warning,
                Title = "Transform gizmo failed",
                Message = "The runtime could not show the transform gizmo of the selection.",
                AffectedScope = new AffectedScope { DocumentId = this.Metadata.DocumentId, DocumentName = this.Metadata.Title },
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }
    }

    private List<SceneNode> FindSelectionRoots(IReadOnlyList<Guid> nodeIds)
    {
        if (this.scene is null || nodeIds.Count == 0)
        {
            return [];
        }

        var selected = nodeIds.ToHashSet();
        var byId = this.scene.AllNodes.Where(node => selected.Contains(node.Id)).ToDictionary(static node => node.Id);
        return [.. nodeIds.Distinct()
            .Select(id => byId.GetValueOrDefault(id))
            .OfType<SceneNode>()
            .Where(node => !node.Ancestors().Any(ancestor => selected.Contains(ancestor.Id)))];
    }

    private void QueueGizmoTargets(GizmoTransaction transaction, IReadOnlyList<RuntimeGizmoTarget> targets)
    {
        // Latest wins: while one preview applies, newer results replace the pending one.
        transaction.Pending = targets;
        if (!transaction.IsPumping)
        {
            transaction.IsPumping = true;
            transaction.Pump = this.PumpGizmoTargetsAsync(transaction);
        }
    }

    private async Task PumpGizmoTargetsAsync(GizmoTransaction transaction)
    {
        try
        {
            while (transaction.Pending is { } targets)
            {
                transaction.Pending = null;
                transaction.Session ??= EditSessionToken.Begin([.. targets.Select(static target => target.NodeId)], GizmoSessionKey);
                transaction.Applied = targets;
                _ = await this.commandService.EditPropertiesForTargetsAsync(
                    transaction.Context,
                    BuildGizmoEdits(transaction.Tool, targets),
                    GizmoLabel(transaction.Tool, targets.Count),
                    transaction.Session).ConfigureAwait(true);
            }
        }
        finally
        {
            transaction.IsPumping = false;
        }
    }

    private async Task CompleteGizmoTransactionAsync(GizmoTransaction transaction, bool commit, IReadOnlyList<RuntimeGizmoTarget>? final)
    {
        if (commit && final is { Count: > 0 })
        {
            this.QueueGizmoTargets(transaction, final);
        }
        else
        {
            transaction.Pending = null;
        }

        await transaction.Pump.ConfigureAwait(true);
        if (transaction.Session is not { } session || transaction.Applied is not { } applied)
        {
            // Nothing moved: there is nothing to keep or restore.
            return;
        }

        // An Alt-drag keeps the originals where they were and copies them to the dragged place.
        var keep = commit && !transaction.Duplicate;
        if (keep)
        {
            session.Commit();
        }
        else
        {
            session.Cancel();
        }

        _ = await this.commandService.EditPropertiesForTargetsAsync(
            transaction.Context,
            BuildGizmoEdits(transaction.Tool, applied),
            GizmoLabel(transaction.Tool, applied.Count),
            session).ConfigureAwait(true);

        if (commit && transaction.Duplicate)
        {
            var transforms = applied.ToDictionary(
                static target => target.NodeId,
                static target => new TransformData { Name = "Transform", Position = target.Position, Rotation = target.Rotation, Scale = target.Scale });
            await this.DuplicateInPlaceAsync(transaction.Context, [.. applied.Select(static target => target.NodeId)], transforms).ConfigureAwait(true);
        }
    }

    private async Task RunViewportEditAsync(ViewportEditCommand command)
    {
        if (this.isDisposed || this.scene is null)
        {
            return;
        }

        switch (command)
        {
            case ViewportEditCommand.Duplicate:
                var selected = this.SelectionService?.GetContext(this.Metadata.DocumentId).SelectedNodeIds ?? [];
                var roots = this.FindSelectionRoots(selected);
                if (roots.Count > 0)
                {
                    await this.DuplicateInPlaceAsync(this.CreateCommandContext(), [.. roots.Select(static node => node.Id)], transforms: null).ConfigureAwait(true);
                }

                break;
            case ViewportEditCommand.Delete:
                _ = this.messenger.Send(new SceneEditRequestMessage(this.Metadata.DocumentId, SceneEditRequest.Delete));
                break;
            case ViewportEditCommand.Undo:
                _ = this.messenger.Send(new SceneEditRequestMessage(this.Metadata.DocumentId, SceneEditRequest.Undo));
                break;
            case ViewportEditCommand.Redo:
                _ = this.messenger.Send(new SceneEditRequestMessage(this.Metadata.DocumentId, SceneEditRequest.Redo));
                break;
            default:
                break;
        }
    }

    /// <summary>Duplicates nodes beside themselves and selects the copies.</summary>
    private async Task DuplicateInPlaceAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        IReadOnlyDictionary<Guid, TransformData>? transforms)
    {
        var result = await this.commandService.DuplicateNodesInPlaceAsync(context, nodeIds, transforms).ConfigureAwait(true);
        if (!result.Succeeded || result.Value is not { Count: > 0 } copies || this.isDisposed
            || this.SelectionService is not { } selection)
        {
            return;
        }

        IReadOnlyList<Guid> ids = [.. copies.Select(static node => node.Id)];
        selection.Publish(
            this.Metadata.DocumentId,
            new SceneSelectionContext(SceneSelectionKind.Node, ids, [], ids[^1], PrimaryFolderId: null),
            ViewportSelectionSource);
    }

    /// <summary>One gizmo drag being applied to the authored scene.</summary>
    private sealed class GizmoTransaction(
        ViewportViewModel viewport,
        RuntimeTransformTool tool,
        bool duplicate,
        SceneDocumentCommandContext context)
    {
        public ViewportViewModel Viewport { get; } = viewport;

        public RuntimeTransformTool Tool { get; } = tool;

        public bool Duplicate { get; } = duplicate;

        /// <summary>Gets the document lifetime the drag edits; a reload ends it.</summary>
        public SceneDocumentCommandContext Context { get; } = context;

        /// <summary>Gets or sets the property gesture, opened by the first change.</summary>
        public EditSessionToken? Session { get; set; }

        /// <summary>Gets or sets the newest results not applied yet.</summary>
        public IReadOnlyList<RuntimeGizmoTarget>? Pending { get; set; }

        /// <summary>Gets or sets the results applied last.</summary>
        public IReadOnlyList<RuntimeGizmoTarget>? Applied { get; set; }

        public bool IsPumping { get; set; }

        public Task Pump { get; set; } = Task.CompletedTask;

        public Task Completion { get; set; } = Task.CompletedTask;
    }
}
