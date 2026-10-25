// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Transform;

/// <summary>
/// Owns captured-target Transform gestures, relative input, serialized requests and pending terminal work.
/// </summary>
/// <remarks>
/// Each control gesture is one scene gesture over the axis it edits: its values preview, and ending it commits or
/// cancels once. A value outside a gesture is one edit.
/// </remarks>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
/// <param name="authoringProvider">The authoring of the scene being inspected.</param>
/// <param name="inputEnabled">The owning model's input/lifetime gate.</param>
/// <param name="refresh">The owning model's value refresh operation.</param>
/// <param name="diagnostics">The borrowed canonical Transform feedback.</param>
internal sealed partial class TransformEditController(
    ILoggerFactory? loggerFactory,
    Func<SceneAuthoring?> authoringProvider,
    Func<bool> inputEnabled,
    Action<ICollection<SceneNode>> refresh,
    InspectorFieldDiagnostics diagnostics) : IDisposable
{
    private const string Label = "Edit Transform";
    private static readonly TimeSpan MouseWheelCommitDelay = TimeSpan.FromMilliseconds(250);
    private static readonly SceneEditResult Nothing = new(SceneEditOutcome.Applied, []);

    private readonly ILogger logger = loggerFactory?.CreateLogger<TransformEditController>() ?? NullLogger<TransformEditController>.Instance;
    private readonly InspectorFieldDiagnostics diagnostics = diagnostics;
    private readonly HashSet<string> supersededFields = [with(StringComparer.Ordinal)];

    private readonly Dictionary<string, TransformEditSession> activeSessions = [with(StringComparer.Ordinal)];
    private readonly Dictionary<string, NumericInputExpression> pendingRelativeEdits = [with(StringComparer.Ordinal)];
    private readonly Dictionary<string, CancellationTokenSource> wheelIdleCommits = [with(StringComparer.Ordinal)];
    private readonly DispatcherQueue? dispatcher = DispatcherQueue.GetForCurrentThread();

    // Keep track of the current selection so property-change handlers can apply edits back
    // to the selected SceneNode instances.
    private ICollection<SceneNode>? selectedItems;

    // Guard against re-entrant callbacks while refreshing values after an authoring request.
    private bool isApplyingEditorChanges;

    // Requests reach the scene in the order they were made.
    private Task<SceneEditResult> lastRequest = Task.FromResult(Nothing);
    private int inFlightEdits;
    private bool isDisposed;

    /// <summary>
    /// Gets the configured logging factory.
    /// </summary>
    public ILoggerFactory? LoggerFactory => loggerFactory;

    /// <summary>Gets completion of in-flight Transform edits.</summary>
    internal Task PendingEdits => this.WaitForPendingEditsAsync();

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
        this.pendingRelativeEdits.Clear();

        foreach (var field in this.activeSessions.Keys.ToArray())
        {
            _ = this.CompleteActiveSessionAsync(field, NumberBoxEditCompletionKind.Cancel);
        }

        GC.SuppressFinalize(this);
    }

    /// <summary>Checks a control value against the generated range of the axis it edits, before it is narrowed.</summary>
    /// <param name="group">The transform vector being edited.</param>
    /// <param name="component">The edited component of the vector.</param>
    /// <param name="value">The value the control holds.</param>
    /// <returns>The result of the check; a value no float holds is out of range.</returns>
    internal static RangeCheck CheckValue(TransformEditFieldGroup group, Component component, double value)
        => Axis(ToPropertyName(group, component)).Check(value);

    /// <summary>Rebinds selection, cancelling captured old-target gestures before accepting later input.</summary>
    /// <param name="items">The new selection.</param>
    internal void BindSelection(ICollection<SceneNode> items)
    {
        if (this.selectedItems is not null && (!this.SelectionMatches([.. items])
            || this.selectedItems.FirstOrDefault()?.Scene != items.FirstOrDefault()?.Scene))
        {
            foreach (var field in this.activeSessions.Keys.ToArray())
            {
                _ = this.pendingRelativeEdits.Remove(field);
                this.supersededFields.Add(field);
                _ = this.CompleteActiveSessionAsync(field, NumberBoxEditCompletionKind.Cancel);
            }

            this.diagnostics.Reset();
        }

        this.selectedItems = items;
    }

    /// <summary>
    /// Starts an interactive transform edit session for one vector component.
    /// </summary>
    /// <param name="group">The transform vector being edited.</param>
    /// <param name="args">The vector-box edit session event arguments.</param>
    internal void BeginEditSession(TransformEditFieldGroup group, VectorBoxEditSessionEventArgs args)
    {
        if (this.isDisposed || !inputEnabled() || this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        var property = ToPropertyName(group, args.Component);
        _ = this.pendingRelativeEdits.Remove(property);
        _ = this.supersededFields.Remove(property);
        if (this.activeSessions.ContainsKey(property) || authoringProvider() is not { } authoring)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        var axis = Axis(property);
        var originalValues = nodes.ToDictionary(
            static node => node.Id,
            node => axis.Read(node.Transform));
        this.activeSessions[property] = new TransformEditSession(
            authoring.BeginGestureAsync([.. nodes.Select(static node => node.Id)], [axis.Id], Label),
            nodes,
            originalValues,
            args.InteractionKind);
    }

    /// <summary>
    /// Completes an interactive transform edit session for one vector component.
    /// </summary>
    /// <param name="group">The transform vector being edited.</param>
    /// <param name="args">The vector-box edit session event arguments.</param>
    internal void CompleteEditSession(TransformEditFieldGroup group, VectorBoxEditSessionEventArgs args)
    {
        if (this.isDisposed)
        {
            return;
        }

        var property = ToPropertyName(group, args.Component);
        if (args.CompletionKind == NumberBoxEditCompletionKind.Commit
            && this.activeSessions.TryGetValue(property, out var activeSession))
        {
            var hasRelativeEdit = this.pendingRelativeEdits.Remove(property, out var relativeEdit);
            if (!hasRelativeEdit && args.InputText is { } inputText)
            {
                var firstValue = activeSession.OriginalValues[activeSession.Nodes[0].Id];
                hasRelativeEdit = NumericInputParser.TryParse(inputText, firstValue, out relativeEdit) && relativeEdit.IsRelative;
            }

            if (hasRelativeEdit)
            {
                _ = this.CompleteRelativeEditSessionAsync(property, relativeEdit, activeSession);
                return;
            }
        }

        if (args.InteractionKind == NumberBoxEditInteractionKind.MouseWheel &&
            args.CompletionKind == NumberBoxEditCompletionKind.Commit)
        {
            this.ScheduleMouseWheelCommit(property);
            return;
        }

        _ = this.CompleteActiveSessionAsync(property, args.CompletionKind ?? NumberBoxEditCompletionKind.Commit);
    }

    /// <summary>Publishes field-specific feedback from VectorBox validation.</summary>
    /// <param name="group">The edited transform group.</param>
    /// <param name="args">The component and validation outcome.</param>
    internal void ReportControlValidation(TransformEditFieldGroup group, ValidationEventArgs<double> args)
    {
        if (!inputEnabled() || this.isApplyingEditorChanges)
        {
            return;
        }

        if (args.Target is not Component component)
        {
            return;
        }

        var field = ToPropertyName(group, component);
        if (!this.activeSessions.ContainsKey(field))
        {
            // Controls also validate while applying templates and displaying model values.
            // Only an edit session can replace feedback from an authoring command.
            return;
        }

        if (args.InputText is { } inputText)
        {
            if (NumericInputParser.TryParse(inputText, args.OldValue, out var expression) && expression.IsRelative)
            {
                this.pendingRelativeEdits[field] = expression;
            }
            else
            {
                _ = this.pendingRelativeEdits.Remove(field);
            }
        }

        var property = Axis(field).Id;
        var ticket = this.diagnostics.Begin([property]);
        var check = CheckValue(group, component, args.NewValue);
        Debug.Assert(args.IsValid == (check == RangeCheck.InRange), "The Transform view validates an axis by its schema range alone.");
        this.diagnostics.Complete(ticket, args.IsValid ? Nothing : Refusal(field, check));
    }

    /// <summary>Submits an absolute value: a preview within the axis's gesture, otherwise one edit.</summary>
    /// <param name="property">The canonical axis key.</param>
    /// <param name="value">The axis value.</param>
    internal void ApplyTransformEdit(string property, double value)
    {
        if (this.isDisposed || !inputEnabled() || this.isApplyingEditorChanges || this.selectedItems is null || authoringProvider() is not { } authoring)
        {
            return;
        }

        if (this.supersededFields.Contains(property) && !this.activeSessions.ContainsKey(property))
        {
            refresh(this.selectedItems);
            return;
        }

        if (this.pendingRelativeEdits.ContainsKey(property))
        {
            return;
        }

        var hasSession = this.activeSessions.TryGetValue(property, out var session);
        var nodes = hasSession ? session!.Nodes : this.selectedItems.ToList();
        var edits = new Dictionary<NodeId, PropertyEdit>();
        foreach (var node in nodes)
        {
            if (!this.TryAxisEdit(property, node, value, out var edit))
            {
                return;
            }

            edits[node.Id] = edit;
        }

        this.LogApplyingChange(property, (float)value, this.selectedItems.Count);
        if (hasSession)
        {
            _ = this.RequestAsync(property, nodes, validates: true, async () =>
                await (await session!.Gesture.ConfigureAwait(true)).PreviewAsync(new SetProperties(edits, Label)).ConfigureAwait(true));
            return;
        }

        _ = this.RequestAsync(property, nodes, validates: true, () => authoring.ExecuteAsync(new SetProperties(edits, Label)));
    }

    /// <summary>Settles hidden drafts, cancelling pointer/wheel work and committing text by existing policy.</summary>
    /// <param name="enabled">Whether input remains active.</param>
    internal void SetInputEnabled(bool enabled)
    {
        if (!enabled)
        {
            foreach (var (field, session) in this.activeSessions.ToArray())
            {
                this.supersededFields.Add(field);
                var completion = session.Interaction == NumberBoxEditInteractionKind.Text
                    ? NumberBoxEditCompletionKind.Commit : NumberBoxEditCompletionKind.Cancel;
                _ = this.CompleteActiveSessionAsync(field, completion);
            }
        }
    }

    private static string ToPropertyName(TransformEditFieldGroup group, Component component)
        => group switch
        {
            TransformEditFieldGroup.Position => component switch
            {
                Component.X => nameof(TransformViewModel.PositionX),
                Component.Y => nameof(TransformViewModel.PositionY),
                _ => nameof(TransformViewModel.PositionZ),
            },
            TransformEditFieldGroup.Rotation => component switch
            {
                Component.X => nameof(TransformViewModel.RotationX),
                Component.Y => nameof(TransformViewModel.RotationY),
                _ => nameof(TransformViewModel.RotationZ),
            },
            _ => component switch
            {
                Component.X => nameof(TransformViewModel.ScaleX),
                Component.Y => nameof(TransformViewModel.ScaleY),
                _ => nameof(TransformViewModel.ScaleZ),
            },
        };

    // The refusal of a value the edit never received, worded as the scene refuses one.
    private static SceneEditResult Refusal(string property, RangeCheck check)
    {
        var id = Axis(property).Id;
        var label = $"{property[..^1]} {property[^1]}";
        return new(SceneEditOutcome.Invalid, [SceneDiagnostic.Error(SceneEditCodes.PropertyOutOfRange, PropertyChecks.Describe(id, check, label), property: id)]);
    }

    private static AxisField Axis(string property)
        => property switch
        {
            "PositionX" => AxisField.Of(SceneProperties.Transform.PositionXDescriptor),
            "PositionY" => AxisField.Of(SceneProperties.Transform.PositionYDescriptor),
            "PositionZ" => AxisField.Of(SceneProperties.Transform.PositionZDescriptor),
            "RotationX" => AxisField.OfRotation(0),
            "RotationY" => AxisField.OfRotation(1),
            "RotationZ" => AxisField.OfRotation(2),
            "ScaleX" => AxisField.Of(SceneProperties.Transform.ScaleXDescriptor),
            "ScaleY" => AxisField.Of(SceneProperties.Transform.ScaleYDescriptor),
            "ScaleZ" => AxisField.Of(SceneProperties.Transform.ScaleZDescriptor),
            _ => throw new ArgumentOutOfRangeException(nameof(property)),
        };

    private async Task CompleteActiveSessionAsync(string property, NumberBoxEditCompletionKind completionKind)
    {
        this.CancelPendingMouseWheelCommit(property);
        _ = this.pendingRelativeEdits.Remove(property);
        if (!this.activeSessions.Remove(property, out var session))
        {
            return;
        }

        await this.EndAsync(property, session, completionKind).ConfigureAwait(true);
    }

    // A terminal request closes the gesture; it does not validate a new field value.
    private Task EndAsync(string property, TransformEditSession session, NumberBoxEditCompletionKind completionKind)
        => this.RequestAsync(property, session.Nodes, validates: false, async () =>
        {
            var gesture = await session.Gesture.ConfigureAwait(true);
            if (completionKind == NumberBoxEditCompletionKind.Cancel)
            {
                await gesture.CancelAsync().ConfigureAwait(true);
                return Nothing;
            }

            return await gesture.CommitAsync().ConfigureAwait(true);
        });

    private void ScheduleMouseWheelCommit(string property)
    {
        this.CancelPendingMouseWheelCommit(property);

        var cts = new CancellationTokenSource();
        this.wheelIdleCommits[property] = cts;
        _ = this.CommitAfterWheelIdleAsync(property, cts.Token);
    }

    private async Task CommitAfterWheelIdleAsync(string property, CancellationToken cancellationToken)
    {
        // Each wheel tick restarts this timer; a superseded delay ends quietly instead of throwing.
        await Task.Delay(MouseWheelCommitDelay, cancellationToken).ConfigureAwait(ConfigureAwaitOptions.SuppressThrowing);
        if (cancellationToken.IsCancellationRequested)
        {
            return;
        }

        if (this.dispatcher is not null)
        {
            _ = this.dispatcher.TryEnqueue(Complete);
            return;
        }

        Complete();

        void Complete()
        {
            // Recheck on the UI thread: another tick or a selection change can
            // invalidate a timer after it has queued its completion.
            if (!cancellationToken.IsCancellationRequested)
            {
                _ = this.CompleteActiveSessionAsync(property, NumberBoxEditCompletionKind.Commit);
            }
        }
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

    // Runs a request after the previous one, then refreshes the fields and reports the result.
    private async Task RequestAsync(string property, IReadOnlyList<SceneNode> nodes, bool validates, Func<Task<SceneEditResult>> request)
    {
        var diagnostic = this.diagnostics.Begin([Axis(property).Id]);
        _ = Interlocked.Increment(ref this.inFlightEdits);
        var previous = this.lastRequest;
        var current = RunAfterAsync(previous, request);
        this.lastRequest = current;
        try
        {
            var result = await current.ConfigureAwait(true);
            if (!this.isDisposed && this.SelectionMatches(nodes))
            {
                this.isApplyingEditorChanges = true;
                try
                {
                    refresh([.. nodes]);
                    if (validates)
                    {
                        this.diagnostics.Complete(diagnostic, result);
                    }
                }
                finally
                {
                    this.isApplyingEditorChanges = false;
                }
            }
        }
        catch (SceneEditRefusedException ex)
        {
            this.LogApplyFailed(property, ex);
        }
        finally
        {
            _ = Interlocked.Decrement(ref this.inFlightEdits);
        }

        static async Task<SceneEditResult> RunAfterAsync(Task<SceneEditResult> previous, Func<Task<SceneEditResult>> request)
        {
            // A failed earlier request does not stop this one; SuppressThrowing needs the non-generic task.
            await ((Task)previous).ConfigureAwait(ConfigureAwaitOptions.ContinueOnCapturedContext | ConfigureAwaitOptions.SuppressThrowing);
            return await request().ConfigureAwait(true);
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

    // A relative entry ("+=5") applies to each target's own starting value, then ends the gesture.
    private async Task CompleteRelativeEditSessionAsync(string property, NumericInputExpression expression, TransformEditSession session)
    {
        this.CancelPendingMouseWheelCommit(property);
        if (!this.activeSessions.Remove(property))
        {
            return;
        }

        var edits = new Dictionary<NodeId, PropertyEdit>();
        foreach (var node in session.Nodes)
        {
            if (!this.TryAxisEdit(property, node, expression.Apply(session.OriginalValues[node.Id]), out var edit))
            {
                await this.EndAsync(property, session, NumberBoxEditCompletionKind.Cancel).ConfigureAwait(true);
                return;
            }

            edits[node.Id] = edit;
        }

        await this.RequestAsync(property, session.Nodes, validates: true, async () =>
            await (await session.Gesture.ConfigureAwait(true)).PreviewAsync(new SetProperties(edits, Label)).ConfigureAwait(true)).ConfigureAwait(true);
        await this.EndAsync(property, session, NumberBoxEditCompletionKind.Commit).ConfigureAwait(true);
    }

    // The edit of a node's axis value as the inspector commits it. A finite value no float holds cannot reach the edit,
    // so it is refused here, out of range, instead of narrowing to an infinity the scene would call not finite.
    private bool TryAxisEdit(string property, SceneNode node, double value, out PropertyEdit edit)
    {
        var axis = Axis(property);
        if (double.IsFinite(value) && Math.Abs(value) > float.MaxValue)
        {
            this.diagnostics.Complete(this.diagnostics.Begin([axis.Id]), Refusal(property, RangeCheck.OutOfRange));
            edit = PropertyEdit.Empty;
            return false;
        }

        edit = InspectorCommits.Commit(axis.Edit(node.Transform, (float)value));
        return true;
    }

    private async Task WaitForPendingEditsAsync()
    {
        while (Volatile.Read(ref this.inFlightEdits) > 0)
        {
            await Task.Yield();
        }
    }

    // One axis of the section: a position or scale axis is a property of its own; a rotation axis is a component of the
    // one rotation property, so its edit writes the node's whole rotation with its other two angles kept.
    private sealed record AxisField(
        PropertyId Id,
        Func<TransformComponent, float> Read,
        Func<TransformComponent, float, PropertyEdit> Edit,
        Func<double, RangeCheck> Check)
    {
        public static AxisField Of(PropertyDescriptor<float> descriptor)
            => new(
                descriptor.Id,
                transform => descriptor.Read(transform),
                (_, value) => PropertyEdit.SingleEdit(descriptor.TypedId, value),
                value => PropertyChecks.Check(descriptor.TypedId, value));

        public static AxisField OfRotation(int axis)
        {
            var descriptor = SceneProperties.Transform.RotationDescriptor;
            var range = PropertyChecks.RangeOf(descriptor.TypedId);
            return new(
                descriptor.Id,
                transform => descriptor.Read(transform)[axis],
                (transform, value) =>
                {
                    var angles = descriptor.Read(transform);
                    angles[axis] = value;
                    return PropertyEdit.SingleEdit(descriptor.TypedId, angles);
                },
                value => !double.IsFinite(value) ? RangeCheck.NotFinite
                    : Math.Abs(value) > float.MaxValue ? RangeCheck.OutOfRange
                    : range.Check((float)value));
        }
    }

    private sealed record TransformEditSession(
        Task<SceneGesture> Gesture,
        IReadOnlyList<SceneNode> Nodes,
        IReadOnlyDictionary<NodeId, float> OriginalValues,
        NumberBoxEditInteractionKind Interaction);
}
