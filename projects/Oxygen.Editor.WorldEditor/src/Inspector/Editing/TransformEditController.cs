// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Editing;

/// <summary>
/// Owns captured-target Transform gestures, relative input, serialized requests and pending terminal work.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
/// <param name="commandService">The existing transform command service.</param>
/// <param name="commandContextProvider">The current document context provider.</param>
/// <param name="inputEnabled">The owning model's input/lifetime gate.</param>
/// <param name="refresh">The owning model's value refresh operation.</param>
/// <param name="diagnostics">The borrowed canonical Transform feedback.</param>
internal sealed partial class TransformEditController(
    ILoggerFactory? loggerFactory,
    ISceneDocumentCommandService? commandService,
    Func<SceneDocumentCommandContext?>? commandContextProvider,
    Func<bool> inputEnabled,
    Action<ICollection<SceneNode>> refresh,
    InspectorFieldDiagnostics diagnostics) : IDisposable
{
    private static readonly TimeSpan MouseWheelCommitDelay = TimeSpan.FromMilliseconds(250);

    private readonly ILogger logger = loggerFactory?.CreateLogger<TransformEditController>() ?? NullLoggerFactory.Instance.CreateLogger<TransformEditController>();
    private readonly InspectorFieldDiagnostics diagnostics = diagnostics;
    private readonly HashSet<string> supersededFields = [];

    private readonly SemaphoreSlim editGate = new(initialCount: 1, maxCount: 1);
    private readonly Dictionary<string, TransformEditSession> activeSessions = [];
    private readonly Dictionary<string, NumericInputExpression> pendingRelativeEdits = [];
    private readonly Dictionary<string, CancellationTokenSource> wheelIdleCommits = [];
    private readonly DispatcherQueue? dispatcher = DispatcherQueue.GetForCurrentThread();

    // Keep track of the current selection so property-change handlers can apply edits back
    // to the selected SceneNode instances.
    private ICollection<SceneNode>? selectedItems;

    // Guard against re-entrant callbacks while refreshing values after an authoring request.
    private bool isApplyingEditorChanges;

    private int inFlightEdits;
    private int editGateDisposed;
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

    /// <summary>Creates an axis-specific edit without supplying untouched components.</summary>
    /// <param name="positionX">Optional X position.</param>
    /// <param name="positionY">Optional Y position.</param>
    /// <param name="positionZ">Optional Z position.</param>
    /// <param name="rotationX">Optional X rotation in degrees.</param>
    /// <param name="rotationY">Optional Y rotation in degrees.</param>
    /// <param name="rotationZ">Optional Z rotation in degrees.</param>
    /// <param name="scaleX">Optional X scale.</param>
    /// <param name="scaleY">Optional Y scale.</param>
    /// <param name="scaleZ">Optional Z scale.</param>
    /// <returns>The partial authored edit.</returns>
    internal static TransformEdit NewEdit(
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
            OptionalEditValues.Unspecified<Vector3>(),
            OptionalEditValues.Unspecified<Vector3>(),
            OptionalEditValues.Unspecified<Vector3>(),
            PositionX: positionX.HasValue ? OptionalEditValues.Supplied<float>(positionX.Value) : OptionalEditValues.Unspecified<float>(),
            PositionY: positionY.HasValue ? OptionalEditValues.Supplied<float>(positionY.Value) : OptionalEditValues.Unspecified<float>(),
            PositionZ: positionZ.HasValue ? OptionalEditValues.Supplied<float>(positionZ.Value) : OptionalEditValues.Unspecified<float>(),
            RotationXDegrees: rotationX.HasValue ? OptionalEditValues.Supplied<float>(rotationX.Value) : OptionalEditValues.Unspecified<float>(),
            RotationYDegrees: rotationY.HasValue ? OptionalEditValues.Supplied<float>(rotationY.Value) : OptionalEditValues.Unspecified<float>(),
            RotationZDegrees: rotationZ.HasValue ? OptionalEditValues.Supplied<float>(rotationZ.Value) : OptionalEditValues.Unspecified<float>(),
            ScaleX: scaleX.HasValue ? OptionalEditValues.Supplied<float>(scaleX.Value) : OptionalEditValues.Unspecified<float>(),
            ScaleY: scaleY.HasValue ? OptionalEditValues.Supplied<float>(scaleY.Value) : OptionalEditValues.Unspecified<float>(),
            ScaleZ: scaleZ.HasValue ? OptionalEditValues.Supplied<float>(scaleZ.Value) : OptionalEditValues.Unspecified<float>());

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
        if (this.activeSessions.ContainsKey(property))
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        var descriptor = TransformDescriptor(property);
        var originalValues = nodes.ToDictionary(
            static node => node.Id,
            node => descriptor.Read(node.Components.OfType<TransformComponent>().Single()));
        var context = commandContextProvider?.Invoke();
        if (context is null || commandService is null)
        {
            return;
        }

        this.activeSessions[property] = new TransformEditSession(
            EditSessionToken.Begin(nodes.ConvertAll(static node => node.Id), property),
            nodes,
            originalValues,
            context,
            args.InteractionKind,
            LastEdit: null);
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
    internal void ReportControlValidation(TransformEditFieldGroup group, ValidationEventArgs<float> args)
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

        var property = TransformDescriptor(field).TypedId.Id;
        var ticket = this.diagnostics.Begin([property], commandContextProvider?.Invoke()?.Metadata.ChangeVersion ?? 0);
        var result = args.IsValid ? SceneCommandResult.Success : new SceneCommandResult(Succeeded: false)
        {
            ValidationCode = "TRANSFORM_INVALID",
            ValidationMessage = group switch
            {
                TransformEditFieldGroup.Position => "Position must be finite.",
                TransformEditFieldGroup.Rotation => "Rotation must be finite and between -180 and 180 degrees.",
                _ => "Scale must be finite and have magnitude at least 0.001.",
            },
        };
        this.diagnostics.Complete(ticket, result);
    }

    /// <summary>Submits an absolute value or preview using the existing target/session policy.</summary>
    /// <param name="property">The canonical axis key.</param>
    /// <param name="edit">The partial axis edit.</param>
    internal void ApplyTransformEdit(string property, TransformEdit edit)
    {
        if (this.isDisposed || !inputEnabled() || this.isApplyingEditorChanges || this.selectedItems is null || commandService is null || commandContextProvider is null)
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

    private static TransformEdit EmptyEdit()
        => new(OptionalEditValues.Unspecified<Vector3>(), OptionalEditValues.Unspecified<Vector3>(), OptionalEditValues.Unspecified<Vector3>());

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

    private static PropertyDescriptor<float> TransformDescriptor(string property)
        => property switch
        {
            "PositionX" => SceneDocumentCommandService.Transform.PositionXDescriptor,
            "PositionY" => SceneDocumentCommandService.Transform.PositionYDescriptor,
            "PositionZ" => SceneDocumentCommandService.Transform.PositionZDescriptor,
            "RotationX" => SceneDocumentCommandService.Transform.RotationXDescriptor,
            "RotationY" => SceneDocumentCommandService.Transform.RotationYDescriptor,
            "RotationZ" => SceneDocumentCommandService.Transform.RotationZDescriptor,
            "ScaleX" => SceneDocumentCommandService.Transform.ScaleXDescriptor,
            "ScaleY" => SceneDocumentCommandService.Transform.ScaleYDescriptor,
            "ScaleZ" => SceneDocumentCommandService.Transform.ScaleZDescriptor,
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
        _ = this.CommitAfterWheelIdleAsync(property, cts.Token);
    }

    private async Task CommitAfterWheelIdleAsync(string property, CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(MouseWheelCommitDelay, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
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

    private async Task ApplyTransformEditAsync(
        string property,
        TransformEdit edit,
        EditSessionToken session,
        IReadOnlyList<SceneNode> nodes,
        SceneDocumentCommandContext? context,
        IReadOnlyDictionary<Guid, PropertyEdit>? targetEdits = null)
    {
        var requestSession = session.Capture();
        var diagnostic = this.diagnostics.Begin([TransformDescriptor(property).TypedId.Id], context?.Metadata.ChangeVersion ?? 0);
        _ = Interlocked.Increment(ref this.inFlightEdits);
        var entered = false;
        try
        {
            await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
            entered = true;

            if (nodes.Count == 0 || context is null || commandService is null)
            {
                return;
            }

            var result = targetEdits is null
                ? await commandService.EditTransformAsync(context, [.. nodes.Select(static node => node.Id)], edit, requestSession).ConfigureAwait(true)
                : await commandService.EditPropertiesForTargetsAsync(context, targetEdits, "Edit Transform", requestSession).ConfigureAwait(true);
            if (!this.isDisposed && this.SelectionMatches(nodes))
            {
                this.isApplyingEditorChanges = true;
                try
                {
                    refresh([.. nodes]);

                    // A terminal request closes the gesture; it does not validate a new field value.
                    if (requestSession.IsOneShot || requestSession.State == EditSessionState.Open)
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
        catch (OperationCanceledException)
        {
        }
        catch (InvalidOperationException ex)
        {
            this.LogApplyFailed(property, ex);
        }
        catch (ArgumentException ex)
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

    private async Task CompleteRelativeEditSessionAsync(string property, NumericInputExpression expression, TransformEditSession session)
    {
        this.CancelPendingMouseWheelCommit(property);
        if (!this.activeSessions.Remove(property))
        {
            return;
        }

        var descriptor = TransformDescriptor(property);
        var edits = session.Nodes.ToDictionary(
            static node => node.Id,
            node => PropertyEdit.Single(
                descriptor.TypedId,
                expression.Apply(session.OriginalValues[node.Id])));
        var empty = EmptyEdit();
        await this.ApplyTransformEditAsync(property, empty, session.Token, session.Nodes, session.Context, edits).ConfigureAwait(true);
        session.Token.Commit();
        await this.ApplyTransformEditAsync(property, empty, session.Token, session.Nodes, session.Context).ConfigureAwait(true);
    }

    private async Task WaitForPendingEditsAsync()
    {
        while (Volatile.Read(ref this.inFlightEdits) > 0)
        {
            await Task.Yield();
        }
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

    private sealed record TransformEditSession(
        EditSessionToken Token,
        IReadOnlyList<SceneNode> Nodes,
        IReadOnlyDictionary<Guid, float> OriginalValues,
        SceneDocumentCommandContext Context,
        NumberBoxEditInteractionKind Interaction,
        TransformEdit? LastEdit);
}
