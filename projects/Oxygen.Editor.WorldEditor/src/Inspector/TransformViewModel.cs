// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for editing the transform properties (position, rotation, scale) of selected SceneNode instances.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class TransformViewModel(
    ILoggerFactory? loggerFactory = null,
    ISceneDocumentCommandService? commandService = null,
    Func<SceneDocumentCommandContext?>? commandContextProvider = null) : ComponentPropertyEditor, IDisposable
{
    private static readonly TimeSpan MouseWheelCommitDelay = TimeSpan.FromMilliseconds(250);

    private readonly ILogger logger = loggerFactory?.CreateLogger<TransformViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<TransformViewModel>();
    private readonly PropertyBinding<float> positionXBinding = new(SceneDocumentCommandService.Transform.PositionXDescriptor);
    private readonly PropertyBinding<float> positionYBinding = new(SceneDocumentCommandService.Transform.PositionYDescriptor);
    private readonly PropertyBinding<float> positionZBinding = new(SceneDocumentCommandService.Transform.PositionZDescriptor);
    private readonly PropertyBinding<float> rotationXBinding = new(SceneDocumentCommandService.Transform.RotationXDescriptor);
    private readonly PropertyBinding<float> rotationYBinding = new(SceneDocumentCommandService.Transform.RotationYDescriptor);
    private readonly PropertyBinding<float> rotationZBinding = new(SceneDocumentCommandService.Transform.RotationZDescriptor);
    private readonly PropertyBinding<float> scaleXBinding = new(SceneDocumentCommandService.Transform.ScaleXDescriptor);
    private readonly PropertyBinding<float> scaleYBinding = new(SceneDocumentCommandService.Transform.ScaleYDescriptor);
    private readonly PropertyBinding<float> scaleZBinding = new(SceneDocumentCommandService.Transform.ScaleZDescriptor);

    // Keep track of the current selection so property-change handlers can apply edits back
    // to the selected SceneNode instances.
    private ICollection<SceneNode>? selectedItems;

    // Guard against re-entrant updates when applying changes from the view back to the model.
    private bool isApplyingEditorChanges;

    private readonly SemaphoreSlim editGate = new(initialCount: 1, maxCount: 1);
    private readonly Dictionary<string, TransformEditSession> activeSessions = [];
    private readonly Dictionary<string, CancellationTokenSource> wheelIdleCommits = [];
    private readonly DispatcherQueue? dispatcher = DispatcherQueue.GetForCurrentThread();
    private int inFlightEdits;
    private int editGateDisposed;
    private bool isDisposed;

    /// <summary>
    ///     Gets exposes the configured <see cref="ILoggerFactory"/> for views to bind to (read-only).
    /// </summary>
    public ILoggerFactory? LoggerFactory => loggerFactory;

    /// <summary>
    ///     Gets or sets the X position of the game object in the scene. The position can be abosulte
    ///     (relative to the scene) or relative to the parent object.
    /// </summary>
    // Position values: non-nullable backing values for VectorBox
    [ObservableProperty]
    public partial float PositionX { get; set; }

    [ObservableProperty]
    public partial bool PositionXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float PositionY { get; set; }

    [ObservableProperty]
    public partial bool PositionYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float PositionZ { get; set; }

    [ObservableProperty]
    public partial bool PositionZIsIndeterminate { get; set; }

    // Rotation values
    [ObservableProperty]
    public partial float RotationX { get; set; }

    [ObservableProperty]
    public partial bool RotationXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float RotationY { get; set; }

    [ObservableProperty]
    public partial bool RotationYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float RotationZ { get; set; }

    [ObservableProperty]
    public partial bool RotationZIsIndeterminate { get; set; }

    // Scale values
    [ObservableProperty]
    public partial float ScaleX { get; set; }

    [ObservableProperty]
    public partial bool ScaleXIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float ScaleY { get; set; }

    [ObservableProperty]
    public partial bool ScaleYIsIndeterminate { get; set; }

    [ObservableProperty]
    public partial float ScaleZ { get; set; }

    [ObservableProperty]
    public partial bool ScaleZIsIndeterminate { get; set; }

    /// <inheritdoc />
    public override string Header => "Transform";

    /// <inheritdoc />
    public override string Description =>
        "Defines the position, rotation and scale of a Game Object along the X, Y and Z axis.";

    /// <summary>
    ///     Gets the property descriptor for the Position property.
    /// </summary>
    public PropertyDescriptor PositionProperty { get; } = new() { Name = "Position" };

    /// <summary>
    ///     Gets the property descriptor for the Rotation property.
    /// </summary>
    public PropertyDescriptor RotationProperty { get; } = new() { Name = "Rotation" };

    /// <summary>
    ///     Gets the property descriptor for the Scale property.
    /// </summary>
    public PropertyDescriptor ScaleProperty { get; } = new() { Name = "Scale" };

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        // Remember selection for two-way change propagation
        this.selectedItems = items;

        this.LogUpdateValues(items.Count);

        this.isApplyingEditorChanges = true;
        try
        {
            this.UpdatePositionValues(items);
            this.UpdateRotationValues(items);
            this.UpdateScaleValues(items);
        }
        finally
        {
            this.isApplyingEditorChanges = false;
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;

        foreach (var pendingCommit in this.wheelIdleCommits.Values)
        {
            pendingCommit.Cancel();
            pendingCommit.Dispose();
        }

        this.wheelIdleCommits.Clear();

        foreach (var sessionEntry in this.activeSessions.ToList())
        {
            sessionEntry.Value.Token.Cancel();
            _ = this.ApplyTransformEditAsync(
                sessionEntry.Key,
                sessionEntry.Value.LastEdit ?? EmptyEdit(),
                sessionEntry.Value.Token,
                sessionEntry.Value.Nodes,
                sessionEntry.Value.Context);
        }

        this.activeSessions.Clear();
        this.TryDisposeEditGate();
        GC.SuppressFinalize(this);
    }

    partial void OnPositionXChanged(float value) => this.ApplyTransformEdit("PositionX", NewEdit(positionX: value));

    partial void OnPositionYChanged(float value) => this.ApplyTransformEdit("PositionY", NewEdit(positionY: value));

    partial void OnPositionZChanged(float value) => this.ApplyTransformEdit("PositionZ", NewEdit(positionZ: value));

    partial void OnRotationXChanged(float value) => this.ApplyTransformEdit("RotationX", NewEdit(rotationX: value));

    partial void OnRotationYChanged(float value) => this.ApplyTransformEdit("RotationY", NewEdit(rotationY: value));

    partial void OnRotationZChanged(float value) => this.ApplyTransformEdit("RotationZ", NewEdit(rotationZ: value));

    partial void OnScaleXChanged(float value) => this.ApplyTransformEdit("ScaleX", NewEdit(scaleX: value));

    partial void OnScaleYChanged(float value) => this.ApplyTransformEdit("ScaleY", NewEdit(scaleY: value));

    partial void OnScaleZChanged(float value) => this.ApplyTransformEdit("ScaleZ", NewEdit(scaleZ: value));

    /// <summary>
    /// Starts an interactive transform edit session for one vector component.
    /// </summary>
    /// <param name="group">The transform vector being edited.</param>
    /// <param name="args">The vector-box edit session event arguments.</param>
    public void BeginEditSession(TransformEditFieldGroup group, VectorBoxEditSessionEventArgs args)
    {
        if (this.isDisposed || this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        var property = ToPropertyName(group, args.Component);
        if (this.activeSessions.ContainsKey(property))
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        var context = commandContextProvider?.Invoke();
        if (context is null || commandService is null)
        {
            return;
        }

        this.activeSessions[property] = new TransformEditSession(
            EditSessionToken.Begin(SceneOperationKinds.EditTransform, nodes.Select(static node => node.Id).ToList(), property),
            nodes,
            context,
            LastEdit: null);
    }

    /// <summary>
    /// Completes an interactive transform edit session for one vector component.
    /// </summary>
    /// <param name="group">The transform vector being edited.</param>
    /// <param name="args">The vector-box edit session event arguments.</param>
    public void CompleteEditSession(TransformEditFieldGroup group, VectorBoxEditSessionEventArgs args)
    {
        if (this.isDisposed)
        {
            return;
        }

        var property = ToPropertyName(group, args.Component);

        if (args.InteractionKind == NumberBoxEditInteractionKind.MouseWheel &&
            args.CompletionKind == NumberBoxEditCompletionKind.Commit)
        {
            this.ScheduleMouseWheelCommit(property);
            return;
        }

        _ = this.CompleteActiveSessionAsync(property, args.CompletionKind ?? NumberBoxEditCompletionKind.Commit);
    }

    private async Task CompleteActiveSessionAsync(string property, NumberBoxEditCompletionKind completionKind)
    {
        this.CancelPendingMouseWheelCommit(property);
        if (!this.activeSessions.Remove(property, out var session))
        {
            return;
        }

        if (completionKind == NumberBoxEditCompletionKind.Cancel)
        {
            session.Token.Cancel();
            await this.ApplyTransformEditAsync(property, session.LastEdit ?? EmptyEdit(), session.Token, session.Nodes, session.Context).ConfigureAwait(true);
            return;
        }

        session.Token.Commit();
        await this.ApplyTransformEditAsync(property, session.LastEdit ?? EmptyEdit(), session.Token, session.Nodes, session.Context).ConfigureAwait(true);
    }

    private void ScheduleMouseWheelCommit(string property)
    {
        this.CancelPendingMouseWheelCommit(property);

        var cts = new CancellationTokenSource();
        this.wheelIdleCommits[property] = cts;
        _ = Task.Run(async () =>
        {
            try
            {
                await Task.Delay(MouseWheelCommitDelay, cts.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }

            void Complete()
                => _ = this.CompleteActiveSessionAsync(property, NumberBoxEditCompletionKind.Commit);

            if (this.dispatcher is not null)
            {
                _ = this.dispatcher.TryEnqueue(Complete);
                return;
            }

            Complete();
        });
    }

    private void CancelPendingMouseWheelCommit(string property)
    {
        if (!this.wheelIdleCommits.Remove(property, out var cts))
        {
            return;
        }

        cts.Cancel();
        cts.Dispose();
    }

    private void ApplyTransformEdit(string property, TransformEdit edit)
    {
        if (this.isDisposed || this.isApplyingEditorChanges || this.selectedItems is null || commandService is null || commandContextProvider is null)
        {
            return;
        }

        this.LogApplyingChange(property, ExtractValue(edit), this.selectedItems.Count);
        if (this.activeSessions.TryGetValue(property, out var existing))
        {
            this.activeSessions[property] = existing with { LastEdit = edit };
            _ = this.ApplyTransformEditAsync(property, edit, existing.Token, existing.Nodes, existing.Context);
            return;
        }

        var nodes = this.selectedItems.ToList();
        var context = commandContextProvider.Invoke();
        _ = this.ApplyTransformEditAsync(property, edit, EditSessionToken.OneShot, nodes, context);
    }

    private async Task ApplyTransformEditAsync(
        string property,
        TransformEdit edit,
        EditSessionToken session,
        IReadOnlyList<SceneNode> nodes,
        SceneDocumentCommandContext? context)
    {
        _ = Interlocked.Increment(ref this.inFlightEdits);
        var entered = false;
        try
        {
            await this.editGate.WaitAsync().ConfigureAwait(true);
            entered = true;

            if (nodes.Count == 0 || context is null || commandService is null)
            {
                return;
            }

            var result = await commandService.EditTransformAsync(
                context,
                nodes.Select(static node => node.Id).ToList(),
                edit,
                session).ConfigureAwait(true);
            if (result.Succeeded && !this.isDisposed && this.SelectionMatches(nodes))
            {
                this.isApplyingEditorChanges = true;
                try
                {
                    this.UpdateValues(nodes.ToList());
                }
                finally
                {
                    this.isApplyingEditorChanges = false;
                }
            }
        }
        catch (Exception ex)
        {
            this.LogApplyFailed(property, ex);
        }
        finally
        {
            if (entered)
            {
                _ = this.editGate.Release();
            }

            _ = Interlocked.Decrement(ref this.inFlightEdits);
            this.TryDisposeEditGate();
        }
    }

    private bool SelectionMatches(IReadOnlyCollection<SceneNode> nodes)
    {
        if (this.selectedItems is null || this.selectedItems.Count != nodes.Count)
        {
            return false;
        }

        var expectedIds = nodes.Select(static node => node.Id).ToHashSet();
        return this.selectedItems.All(node => expectedIds.Contains(node.Id));
    }

    private void TryDisposeEditGate()
    {
        if (!this.isDisposed || Volatile.Read(ref this.inFlightEdits) != 0)
        {
            return;
        }

        if (Interlocked.Exchange(ref this.editGateDisposed, 1) == 0)
        {
            this.editGate.Dispose();
        }
    }

    private static TransformEdit EmptyEdit()
        => new(Optional<Vector3>.Unspecified, Optional<Vector3>.Unspecified, Optional<Vector3>.Unspecified);

    private static TransformEdit NewEdit(
        float? positionX = null,
        float? positionY = null,
        float? positionZ = null,
        float? rotationX = null,
        float? rotationY = null,
        float? rotationZ = null,
        float? scaleX = null,
        float? scaleY = null,
        float? scaleZ = null)
        => new(
            Optional<Vector3>.Unspecified,
            Optional<Vector3>.Unspecified,
            Optional<Vector3>.Unspecified,
            PositionX: positionX.HasValue ? Optional<float>.Supplied(positionX.Value) : Optional<float>.Unspecified,
            PositionY: positionY.HasValue ? Optional<float>.Supplied(positionY.Value) : Optional<float>.Unspecified,
            PositionZ: positionZ.HasValue ? Optional<float>.Supplied(positionZ.Value) : Optional<float>.Unspecified,
            RotationXDegrees: rotationX.HasValue ? Optional<float>.Supplied(rotationX.Value) : Optional<float>.Unspecified,
            RotationYDegrees: rotationY.HasValue ? Optional<float>.Supplied(rotationY.Value) : Optional<float>.Unspecified,
            RotationZDegrees: rotationZ.HasValue ? Optional<float>.Supplied(rotationZ.Value) : Optional<float>.Unspecified,
            ScaleX: scaleX.HasValue ? Optional<float>.Supplied(scaleX.Value) : Optional<float>.Unspecified,
            ScaleY: scaleY.HasValue ? Optional<float>.Supplied(scaleY.Value) : Optional<float>.Unspecified,
            ScaleZ: scaleZ.HasValue ? Optional<float>.Supplied(scaleZ.Value) : Optional<float>.Unspecified);

    private static float ExtractValue(TransformEdit edit)
        => edit.PositionX.HasValue ? edit.PositionX.Value! :
           edit.PositionY.HasValue ? edit.PositionY.Value! :
           edit.PositionZ.HasValue ? edit.PositionZ.Value! :
           edit.RotationXDegrees.HasValue ? edit.RotationXDegrees.Value! :
           edit.RotationYDegrees.HasValue ? edit.RotationYDegrees.Value! :
           edit.RotationZDegrees.HasValue ? edit.RotationZDegrees.Value! :
           edit.ScaleX.HasValue ? edit.ScaleX.Value! :
           edit.ScaleY.HasValue ? edit.ScaleY.Value! :
           edit.ScaleZ.HasValue ? edit.ScaleZ.Value! :
           0f;

    private static string ToPropertyName(TransformEditFieldGroup group, Component component)
        => group switch
        {
            TransformEditFieldGroup.Position => component switch
            {
                Component.X => nameof(PositionX),
                Component.Y => nameof(PositionY),
                _ => nameof(PositionZ),
            },
            TransformEditFieldGroup.Rotation => component switch
            {
                Component.X => nameof(RotationX),
                Component.Y => nameof(RotationY),
                _ => nameof(RotationZ),
            },
            _ => component switch
            {
                Component.X => nameof(ScaleX),
                Component.Y => nameof(ScaleY),
                _ => nameof(ScaleZ),
            },
        };

    private void UpdatePositionValues(ICollection<SceneNode> items)
    {
        this.UpdateBindingValue(this.positionXBinding, items, value => this.PositionX = value, mixed => this.PositionXIsIndeterminate = mixed);
        this.UpdateBindingValue(this.positionYBinding, items, value => this.PositionY = value, mixed => this.PositionYIsIndeterminate = mixed);
        this.UpdateBindingValue(this.positionZBinding, items, value => this.PositionZ = value, mixed => this.PositionZIsIndeterminate = mixed);
    }

    private void UpdateRotationValues(ICollection<SceneNode> items)
    {
        this.UpdateBindingValue(this.rotationXBinding, items, value => this.RotationX = value, mixed => this.RotationXIsIndeterminate = mixed);
        this.UpdateBindingValue(this.rotationYBinding, items, value => this.RotationY = value, mixed => this.RotationYIsIndeterminate = mixed);
        this.UpdateBindingValue(this.rotationZBinding, items, value => this.RotationZ = value, mixed => this.RotationZIsIndeterminate = mixed);
    }

    private void UpdateScaleValues(ICollection<SceneNode> items)
    {
        this.UpdateBindingValue(
            this.scaleXBinding,
            items,
            value => this.ScaleX = value,
            mixed => this.ScaleXIsIndeterminate = mixed,
            this.NormalizeScaleBindingValue);
        this.UpdateBindingValue(
            this.scaleYBinding,
            items,
            value => this.ScaleY = value,
            mixed => this.ScaleYIsIndeterminate = mixed,
            this.NormalizeScaleBindingValue);
        this.UpdateBindingValue(
            this.scaleZBinding,
            items,
            value => this.ScaleZ = value,
            mixed => this.ScaleZIsIndeterminate = mixed,
            this.NormalizeScaleBindingValue);
    }

    private void UpdateBindingValue(
        PropertyBinding<float> binding,
        ICollection<SceneNode> items,
        Action<float> setValue,
        Action<bool> setMixed,
        Func<PropertyBinding<float>, float>? displayValue = null)
    {
        var nodeIds = items.Select(static node => node.Id).ToList();
        var targets = items.ToDictionary(static node => node.Id, static node => (object?)node.Components.OfType<TransformComponent>().FirstOrDefault());
        binding.UpdateFromModel(nodeIds, id => targets.GetValueOrDefault(id));

        if (!binding.HasValue)
        {
            setValue(0);
            setMixed(false);
            return;
        }

        setValue(displayValue?.Invoke(binding) ?? binding.Value);
        setMixed(binding.IsMixed);
    }

    private float NormalizeScaleBindingValue(PropertyBinding<float> binding)
        => binding.IsMixed ? TransformConverter.NormalizeScaleValue(binding.Value) : binding.Value;

    private sealed record TransformEditSession(
        EditSessionToken Token,
        IReadOnlyList<SceneNode> Nodes,
        SceneDocumentCommandContext Context,
        TransformEdit? LastEdit);
}
