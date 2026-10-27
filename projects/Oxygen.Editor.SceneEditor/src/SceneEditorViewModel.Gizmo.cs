// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// The transform gizmo and scene helpers of the scene's selection: which nodes they act on, and
/// applying their drags through the authoring commands so a drag previews live, commits one undo
/// entry and cancels exactly.
/// </summary>
/// <remarks>
/// The engine hit tests and drags the gizmo and the light helpers' handles, and reports each drag's
/// new local transforms or light value; this view model is the only writer of the authored scene.
/// A node an instance brings in moves and has its light edited like a node of the scene, through
/// its instance root's changes; Alt-drag refuses it, as Duplicate does. An Alt-drag previews on the originals and, on release, restores them and duplicates them at the
/// dragged transforms, so a cancelled Alt-drag leaves no copy and no history. Icons, helpers and
/// the orientation triad are engine overlays: they never reach the authored scene.
/// </remarks>
public partial class SceneEditorViewModel
{
    private SynchronizationContext? gizmoContext;
    private GizmoTransaction? gizmoTransaction;

    /// <summary>Gets the viewport tools shared by the scene's panes.</summary>
    public TransformToolsViewModel TransformTools { get; private set; } = null!;

    /// <summary>Builds the property edits that give each target its dragged transform.</summary>
    /// <param name="tool">The dragged gizmo's tool: it decides which properties the drag changes.</param>
    /// <param name="targets">The targets' new local transforms.</param>
    /// <returns>One edit per target, nodes an instance brings in included.</returns>
    internal static Dictionary<NodeReference, PropertyEdit> BuildGizmoEdits(RuntimeTransformTool tool, IReadOnlyList<RuntimeGizmoTarget> targets)
    {
        var edits = new Dictionary<NodeReference, PropertyEdit>(targets.Count);
        foreach (var target in targets)
        {
            // Every drag moves positions: rotation and scale act about the pivot.
            var edit = new PropertyEdit.Builder()
                .Set(SceneSchema.Transform.PositionX, target.Position.X)
                .Set(SceneSchema.Transform.PositionY, target.Position.Y)
                .Set(SceneSchema.Transform.PositionZ, target.Position.Z);
            if (tool == RuntimeTransformTool.Rotate)
            {
                var euler = TransformConverter.QuaternionToEulerDegrees(target.Rotation);
                _ = edit.Set(SceneSchema.Transform.Rotation, euler);
            }
            else if (tool == RuntimeTransformTool.Scale)
            {
                _ = edit.Set(SceneSchema.Transform.ScaleX, target.Scale.X).Set(SceneSchema.Transform.ScaleY, target.Scale.Y).Set(SceneSchema.Transform.ScaleZ, target.Scale.Z);
            }

            edits[target.Node] = edit.Build();
        }

        return edits;
    }

    /// <summary>Builds the property edit a light helper's handle drag applies.</summary>
    /// <param name="light">The kind of the dragged light, as the engine reports it.</param>
    /// <param name="handle">The dragged handle.</param>
    /// <param name="value">Metres for a range, radians for a cone angle.</param>
    /// <returns>The edit, or <see langword="null"/> when the light has no such value.</returns>
    internal static PropertyEdit? BuildHelperEdit(RuntimeHelperLight light, RuntimeHelperHandle handle, float value)
    {
        var descriptor = (light, handle) switch
        {
            (RuntimeHelperLight.Point, RuntimeHelperHandle.Range) => SceneProperties.PointLight.RangeDescriptor,
            (RuntimeHelperLight.Spot, RuntimeHelperHandle.Range) => SceneProperties.SpotLight.RangeDescriptor,
            (RuntimeHelperLight.Spot, RuntimeHelperHandle.InnerCone) => SceneProperties.SpotLight.InnerConeAngleRadiansDescriptor,
            (RuntimeHelperLight.Spot, RuntimeHelperHandle.OuterCone) => SceneProperties.SpotLight.OuterConeAngleRadiansDescriptor,
            _ => null,
        };
        if (descriptor is null)
        {
            return null;
        }

        return PropertyEdit.SingleEdit(new PropertyId<float>(descriptor.Id), value);
    }

    /// <summary>Applies one gizmo or helper interaction reported by the engine; runs on the UI thread.</summary>
    /// <param name="gizmoEvent">The interaction.</param>
    internal void ApplyGizmoEvent(RuntimeGizmoEvent gizmoEvent)
    {
        if (this.isDisposed)
        {
            return;
        }

        var viewport = this.Viewports.FirstOrDefault(pane => pane.AssignedViewId == gizmoEvent.ViewId);
        if (gizmoEvent.Helper)
        {
            this.ApplyHelperEvent(viewport, gizmoEvent);
            return;
        }

        switch (gizmoEvent.Kind)
        {
            case RuntimeGizmoEventKind.Hover:
                viewport?.SetGizmoHover(gizmoEvent.Hovering);
                break;
            case RuntimeGizmoEventKind.Begin when gizmoEvent.Duplicate && this.RefusesInstancedStructure(ViewportEditCommand.Duplicate):
                // An Alt-drag duplicates its targets, which a node an instance brings in refuses.
                _ = this.services.Engine.CancelTransformGizmoDragAsync();
                break;
            case RuntimeGizmoEventKind.Begin when viewport is not null && this.Authoring is { } authoring:
                this.gizmoTransaction = new GizmoTransaction(viewport, gizmoEvent.Tool, gizmoEvent.Duplicate, authoring);
                viewport.BeginGizmoDrag();
                break;
            case RuntimeGizmoEventKind.Update when this.gizmoTransaction is { } transaction && ReferenceEquals(transaction.Viewport, viewport):
                transaction.Viewport.UpdateGizmoDrag(gizmoEvent);
                if (gizmoEvent.Representable)
                {
                    this.QueueGizmoEdits(transaction, TransformEdits(transaction.Tool, gizmoEvent.Targets));
                }

                break;
            case RuntimeGizmoEventKind.Commit or RuntimeGizmoEventKind.Cancel
                when this.gizmoTransaction is { } transaction && ReferenceEquals(transaction.Viewport, viewport):
                this.gizmoTransaction = null;
                transaction.Viewport.EndGizmoDrag();
                transaction.Completion = this.CompleteGizmoTransactionAsync(
                    transaction,
                    gizmoEvent.Kind == RuntimeGizmoEventKind.Commit,
                    gizmoEvent.Representable ? TransformEdits(transaction.Tool, gizmoEvent.Targets) : null);
                break;
            default:
                break;
        }
    }

    /// <summary>
    /// The nodes the gizmo moves, nodes an instance brings in included: the selection without locked nodes, and without
    /// nodes whose ancestor, in their instance or in the scene, is selected too.
    /// </summary>
    /// <param name="activeNode">The node whose origin is the pivot.</param>
    /// <returns>The targets, in selection order.</returns>
    internal IReadOnlyList<NodeReference> GetGizmoTargets(out NodeReference? activeNode)
    {
        activeNode = null;
        if (this.scene is not { } scene)
        {
            return [];
        }

        var context = this.SelectionService.GetContext(this.Document.Id);
        var selected = context.SelectedNodes.Where(scene.Holds).Distinct().ToList();
        var interaction = this.Interaction;
        var targets = selected
            .Where(node => !(interaction.GetAncestors(scene, node) ?? []).Any(selected.Contains) && !interaction.GetLock(scene, node).Protects)
            .ToList();
        activeNode = context.PrimaryNode is { } primary && targets.Contains(primary) ? primary
            : targets.Count > 0 ? targets[^1] : null;
        return targets;
    }

    private static GizmoEdits TransformEdits(RuntimeTransformTool tool, IReadOnlyList<RuntimeGizmoTarget> targets)
        => new(BuildGizmoEdits(tool, targets), GizmoLabel(tool, targets.Count), targets);

    private static string HelperLabel(RuntimeHelperHandle handle) => handle switch
    {
        RuntimeHelperHandle.InnerCone => "Inner cone angle",
        RuntimeHelperHandle.OuterCone => "Outer cone angle",
        _ => "Light range",
    };

    private static string GizmoLabel(RuntimeTransformTool tool, int count)
    {
        var verb = tool switch
        {
            RuntimeTransformTool.Rotate => "Rotate",
            RuntimeTransformTool.Scale => "Scale",
            _ => "Move",
        };
        return count == 1 ? verb : string.Create(CultureInfo.InvariantCulture, $"{verb} {count} nodes");
    }

    private void InitializeTransformTools()
    {
        this.gizmoContext = SynchronizationContext.Current;
        var settings = this.services.TransformTools;
        this.TransformTools = new TransformToolsViewModel(settings);
        this.TransformTools.Changed += this.OnTransformToolsChanged;
        this.services.Engine.GizmoEvent += this.OnEngineGizmoEvent;
        this.Interaction.StateChanged += this.OnInteractionStateChanged;

        _ = settings.EnsureLoadedAsync();
    }

    private void DisposeTransformTools()
    {
        this.services.Engine.GizmoEvent -= this.OnEngineGizmoEvent;
        this.Interaction.StateChanged -= this.OnInteractionStateChanged;

        this.TransformTools.Changed -= this.OnTransformToolsChanged;
        this.TransformTools.Dispose();
    }

    private void AttachGizmoServices(ViewportViewModel viewport)
    {
        viewport.TransformTools = this.TransformTools;
        viewport.EditCommandRequested = this.RunViewportEditAsync;
        viewport.DisplayScaleChanged = this.UpdateTransformGizmo;
    }

    private void ApplyHelperEvent(ViewportViewModel? viewport, RuntimeGizmoEvent gizmoEvent)
    {
        switch (gizmoEvent.Kind)
        {
            case RuntimeGizmoEventKind.Hover:
                viewport?.SetHelperHover(gizmoEvent.Hovering);
                break;
            case RuntimeGizmoEventKind.ViewAxis when viewport is not null:
                _ = viewport.SelectAxisViewAsync(gizmoEvent.Handle);
                break;
            case RuntimeGizmoEventKind.Begin when viewport is not null && this.Authoring is { } authoring:
                this.gizmoTransaction = new GizmoTransaction(viewport, RuntimeTransformTool.Select, duplicate: false, authoring);
                viewport.BeginGizmoDrag();
                viewport.UpdateHelperDrag(gizmoEvent);
                break;
            case RuntimeGizmoEventKind.Update when this.gizmoTransaction is { } transaction && ReferenceEquals(transaction.Viewport, viewport):
                transaction.Viewport.UpdateHelperDrag(gizmoEvent);
                if (this.HelperEdits(gizmoEvent) is { } edits)
                {
                    this.QueueGizmoEdits(transaction, edits);
                }

                break;
            case RuntimeGizmoEventKind.Commit or RuntimeGizmoEventKind.Cancel
                when this.gizmoTransaction is { } transaction && ReferenceEquals(transaction.Viewport, viewport):
                this.gizmoTransaction = null;
                transaction.Viewport.EndGizmoDrag();
                transaction.Completion = this.CompleteGizmoTransactionAsync(
                    transaction,
                    gizmoEvent.Kind == RuntimeGizmoEventKind.Commit,
                    this.HelperEdits(gizmoEvent));
                break;
            default:
                break;
        }
    }

    private GizmoEdits? HelperEdits(RuntimeGizmoEvent gizmoEvent)
        => this.scene is { } scene
            && gizmoEvent.Node is { } node
            && scene.Holds(node)
            && BuildHelperEdit(gizmoEvent.Light, gizmoEvent.HelperHandle, gizmoEvent.Value) is { } edit
            ? new(new Dictionary<NodeReference, PropertyEdit> { [node] = edit }, HelperLabel(gizmoEvent.HelperHandle), [])
            : null;

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

        var targets = this.GetGizmoTargets(out var activeNode);
        var settings = this.TransformTools.Settings;
        var displayScale = (float)(this.GetActiveViewport()?.DisplayScale ?? 1.0);
        var gizmo = new RuntimeTransformGizmo(
            this.TransformTools.Tool,
            settings.Space,
            settings.Snap,
            targets,
            activeNode,
            displayScale);
        _ = this.SetTransformGizmoAsync(gizmo);
        _ = this.SetSceneHelpersAsync(displayScale);
    }

    /// <summary>Tells the helpers which nodes are hidden, and which selected ones are locked.</summary>
    private async Task SetSceneHelpersAsync(float displayScale)
    {
        var interaction = this.Interaction;
        var selected = this.SelectionService.GetContext(this.Document.Id).SelectedNodes;
        IReadOnlyList<NodeReference> locked = this.scene is not { } scene
            ? []
            : [.. selected.Where(node => scene.Holds(node) && interaction.GetLock(scene, node).Protects)];
        var helpers = new RuntimeSceneHelpers([.. interaction.HiddenNodes()], locked, displayScale);
        try
        {
            if (!await this.services.Engine.SetSceneHelpersAsync(helpers).ConfigureAwait(true))
            {
                this.LogSceneHelpersRejected();
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogSceneHelpersFailed(ex);
        }
    }

    private async Task SetTransformGizmoAsync(RuntimeTransformGizmo gizmo)
    {
        try
        {
            if (!await this.services.Engine.SetTransformGizmoAsync(gizmo).ConfigureAwait(true))
            {
                this.LogTransformGizmoRejected();
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.services.OperationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = RuntimeOperationKinds.TransformGizmo,
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Warning,
                Title = "Transform gizmo failed",
                Message = "The runtime could not show the transform gizmo of the selection.",
                AffectedScope = this.SceneScope(),
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }
    }

    private List<SceneNode> FindSelectionRoots(IReadOnlyList<NodeId> nodeIds)
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

    private void QueueGizmoEdits(GizmoTransaction transaction, GizmoEdits edits)
    {
        // Latest wins: while one preview applies, newer results replace the pending one.
        transaction.Pending = edits;
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
            while (transaction.Pending is { } edits)
            {
                transaction.Pending = null;
                transaction.Gesture ??= transaction.Authoring.BeginNodeGestureAsync(
                    [.. edits.Edits.Keys],
                    [.. edits.Edits.Values.SelectMany(static edit => edit.Ids).Distinct()],
                    edits.Label);
                transaction.Applied = edits;
                var gesture = await transaction.Gesture.ConfigureAwait(true);
                _ = await gesture.PreviewAsync(edits.Edits).ConfigureAwait(true);
            }
        }
        finally
        {
            transaction.IsPumping = false;
        }
    }

    private async Task CompleteGizmoTransactionAsync(GizmoTransaction transaction, bool commit, GizmoEdits? final)
    {
        if (commit && final is { Edits.Count: > 0 })
        {
            this.QueueGizmoEdits(transaction, final);
        }
        else
        {
            transaction.Pending = null;
        }

        await transaction.Pump.ConfigureAwait(true);
        if (transaction.Gesture is not { } started || transaction.Applied is not { } applied)
        {
            // Nothing moved: there is nothing to keep or restore.
            return;
        }

        // An Alt-drag keeps the originals where they were and copies them to the dragged place.
        var gesture = await started.ConfigureAwait(true);
        if (commit && !transaction.Duplicate)
        {
            _ = await gesture.CommitAsync().ConfigureAwait(true);
        }
        else
        {
            await gesture.CancelAsync().ConfigureAwait(true);
        }

        if (commit && transaction.Duplicate)
        {
            // An Alt-drag of a node an instance brings in was refused when it began: the targets are the scene's own.
            var transforms = applied.Targets.ToDictionary(
                static target => target.Node.Node,
                static target => new TransformData { Name = "Transform", Position = target.Position, Rotation = target.Rotation, Scale = target.Scale });
            await this.DuplicateInPlaceAsync(transaction.Authoring, [.. transforms.Keys], transforms).ConfigureAwait(true);
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
            case ViewportEditCommand.Duplicate when this.RefusesInstancedStructure(command):
            case ViewportEditCommand.Delete when this.RefusesInstancedStructure(command):
                break;
            case ViewportEditCommand.Duplicate:
                var selected = this.SelectionService.GetContext(this.Document.Id).OwnedNodeIds;
                var roots = this.FindSelectionRoots(selected);
                if (roots.Count > 0 && this.Authoring is { } authoring)
                {
                    await this.DuplicateInPlaceAsync(authoring, [.. roots.Select(static node => node.Id)], transforms: null).ConfigureAwait(true);
                }

                break;
            case ViewportEditCommand.Delete:
                await this.RunShellCommandAsync(ShellCommands.Delete).ConfigureAwait(true);
                break;
            case ViewportEditCommand.Undo:
                await this.RunShellCommandAsync(ShellCommands.Undo).ConfigureAwait(true);
                break;
            case ViewportEditCommand.Redo:
                await this.RunShellCommandAsync(ShellCommands.Redo).ConfigureAwait(true);
                break;
            default:
                break;
        }
    }

    /// <summary>Duplicates nodes beside themselves and selects the copies.</summary>
    private async Task DuplicateInPlaceAsync(
        SceneAuthoring authoring,
        IReadOnlyList<NodeId> nodeIds,
        IReadOnlyDictionary<NodeId, TransformData>? transforms)
    {
        if (SceneNodeCopies.DuplicateInPlace(authoring.Scene, authoring.Layout, nodeIds, transforms) is not { } duplicate)
        {
            return;
        }

        var result = await authoring.ExecuteAsync(duplicate).ConfigureAwait(true);
        if (!result.Succeeded || this.isDisposed)
        {
            return;
        }

        this.SelectionService.Publish(
            this.Document.Id,
            SceneSelectionContext.ForNodes(duplicate.Inserts.Select(static insert => NodeReference.To(insert.Node.Id))),
            ViewportSelectionSource);
    }

    /// <summary>One gizmo drag being applied to the authored scene.</summary>
    private sealed class GizmoTransaction(
        ViewportViewModel viewport,
        RuntimeTransformTool tool,
        bool duplicate,
        SceneAuthoring authoring)
    {
        public ViewportViewModel Viewport { get; } = viewport;

        public RuntimeTransformTool Tool { get; } = tool;

        public bool Duplicate { get; } = duplicate;

        /// <summary>Gets the authoring the drag edits; a reload replaces it and ends the drag.</summary>
        public SceneAuthoring Authoring { get; } = authoring;

        /// <summary>Gets or sets the scene gesture, opened by the first change.</summary>
        public Task<SceneGesture>? Gesture { get; set; }

        /// <summary>Gets or sets the newest results not applied yet.</summary>
        public GizmoEdits? Pending { get; set; }

        /// <summary>Gets or sets the results applied last.</summary>
        public GizmoEdits? Applied { get; set; }

        public bool IsPumping { get; set; }

        public Task Pump { get; set; } = Task.CompletedTask;

        public Task Completion { get; set; } = Task.CompletedTask;
    }

    /// <summary>The property edits one drag result applies, with its history label.</summary>
    /// <param name="Edits">One edit per node, nodes an instance brings in included.</param>
    /// <param name="Label">The undo entry's label.</param>
    /// <param name="Targets">A transform drag's results, which an Alt-drag duplicates at; empty for helpers.</param>
    private sealed record GizmoEdits(
        IReadOnlyDictionary<NodeReference, PropertyEdit> Edits,
        string Label,
        IReadOnlyList<RuntimeGizmoTarget> Targets);
}
