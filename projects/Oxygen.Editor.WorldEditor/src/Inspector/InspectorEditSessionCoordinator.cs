// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Captures control targets and delivers one terminal command per gesture.</summary>
internal sealed partial class InspectorEditSessionCoordinator(
    ISceneDocumentCommandService service,
    Func<SceneDocumentCommandContext?> contextProvider,
    string label,
    Action refresh,
    bool environment = false,
    InspectorFieldDiagnostics? diagnostics = null) : IDisposable
{
    private readonly List<Task> pending = [];
    // Node targets; in environment mode the target is the bound context's scene and this stays empty.
    private NodeId[] targets = [];
    private SceneDocumentCommandContext? boundContext;
    private Gesture? active;
    private HashSet<PropertyId>? supersededProperties;
    private bool disposed;
    private bool refreshingResult;
    private bool inputEnabled = true;

    /// <summary>Gets the current diagnostics for the bound fields.</summary>
    public InspectorFieldDiagnostics Diagnostics { get; } = diagnostics ?? new();

    /// <summary>Gets the identity of the current binding lifetime.</summary>
    public Guid ScopeId { get; private set; } = Guid.NewGuid();

    /// <summary>Gets completion of already submitted edits.</summary>
    public Task Pending => this.DrainAsync();

    /// <summary>Ends hidden control gestures while retaining target-scoped feedback and pending results.</summary>
    /// <param name="enabled">Whether new control input may be submitted.</param>
    public void SetInputEnabled(bool enabled)
    {
        if (this.inputEnabled == enabled)
        {
            return;
        }

        this.inputEnabled = enabled;
        this.ScopeId = Guid.NewGuid();
        if (!enabled)
        {
            this.supersededProperties = this.active?.Properties;
            var completion = this.active?.Interaction == NumberBoxEditInteractionKind.Text ? NumberBoxEditCompletionKind.Commit : NumberBoxEditCompletionKind.Cancel;
            this.End(completion);
            if (completion == NumberBoxEditCompletionKind.Cancel && this.supersededProperties is { } properties)
            {
                foreach (var property in properties)
                {
                    this.Diagnostics.Invalidate(property);
                }
            }
        }
    }

    /// <summary>Updates the binding scope, ending the old gesture first.</summary>
    /// <param name="nodeIds">The bound target identities.</param>
    public void Bind(IReadOnlyList<NodeId> nodeIds)
    {
        var context = contextProvider();
        if (this.IsBoundTo(context) && this.targets.ToHashSet().SetEquals(nodeIds))
        {
            return;
        }

        this.supersededProperties = this.active?.Properties;
        var completion = this.active?.Interaction == NumberBoxEditInteractionKind.Text ? NumberBoxEditCompletionKind.Commit : NumberBoxEditCompletionKind.Cancel;

        // Cancellation changes the old model and can synchronously reenter Bind
        // through the inspector host. Publish the new scope before that callback.
        this.boundContext = context;
        this.targets = nodeIds.ToArray();
        this.ScopeId = Guid.NewGuid();
        this.Diagnostics.Reset();
        this.End(completion);
    }

    /// <summary>Begins a control-owned gesture with stable document and target identities.</summary>
    /// <param name="field">The edited field key.</param>
    /// <param name="interaction">The control interaction that owns the gesture.</param>
    public void Begin(string field, NumberBoxEditInteractionKind interaction)
    {
        if (this.disposed || !this.inputEnabled || contextProvider() is not { } context || (!environment && this.targets.Length == 0))
        {
            return;
        }

        if (this.active is { } current && string.Equals(current.Field, field, StringComparison.Ordinal) && current.Token.State == EditSessionState.Open)
        {
            current.Idle?.Cancel();
            return;
        }

        this.End(NumberBoxEditCompletionKind.Commit);
        this.supersededProperties = null;
        this.active = new(
            context,
            this.targets,
            field,
            interaction,
            EditSessionToken.Begin(this.targets, field));
    }

    /// <summary>Completes numeric edits, coalescing wheel events for 250 milliseconds.</summary>
    /// <param name="args">The control completion event.</param>
    public void Complete(NumberBoxEditSessionEventArgs args)
    {
        if (args.InteractionKind == NumberBoxEditInteractionKind.MouseWheel && args.CompletionKind == NumberBoxEditCompletionKind.Commit && this.active is { } gesture)
        {
            gesture.Idle?.Cancel();
            gesture.Idle = new CancellationTokenSource();
            this.Track(this.CommitAfterIdleAsync(gesture, gesture.Idle));
            return;
        }

        this.End(args.CompletionKind ?? NumberBoxEditCompletionKind.Commit);
    }

    /// <summary>Submits uniform property values without delaying the authoring transaction.</summary>
    /// <param name="edit">The uniform requested values.</param>
    public void Submit(PropertyEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        this.Submit(this.targets.ToDictionary(id => id, _ => edit.Clone()), uniform: edit);
    }

    /// <summary>Submits per-target values for color axes and world-space sun orientation.</summary>
    /// <param name="edits">The requested values for each target.</param>
    public void Submit(IReadOnlyDictionary<NodeId, PropertyEdit> edits)
        => this.Submit(edits, uniform: null);

    /// <summary>Ends the current gesture exactly once.</summary>
    /// <param name="completion">Whether to commit or restore the original values.</param>
    public void End(NumberBoxEditCompletionKind completion)
    {
        if (this.active is not { } gesture)
        {
            return;
        }

        this.active = null;
        gesture.Idle?.Cancel();
        if (completion == NumberBoxEditCompletionKind.Cancel)
        {
            gesture.Token.Cancel();
        }
        else
        {
            gesture.Token.Commit();
        }

        var empty = gesture.Targets.ToDictionary(id => id, _ => PropertyEdit.Empty);
        this.Track(this.ObserveAsync(this.Send(gesture.Context, empty, PropertyEdit.Empty, gesture.Token), gesture.Context, gesture.Targets, ticket: null));
    }

    /// <summary>Clears field feedback after an external model change.</summary>
    /// <param name="property">The property refreshed from the model.</param>
    public void ModelChanged(PropertyId property)
    {
        if (!this.refreshingResult)
        {
            this.Diagnostics.Invalidate(property);
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.End(NumberBoxEditCompletionKind.Cancel);
        this.disposed = true;
    }

    private void Submit(IReadOnlyDictionary<NodeId, PropertyEdit> edits, PropertyEdit? uniform)
    {
        if (this.disposed || !this.inputEnabled || contextProvider() is not { } context || !this.IsBoundTo(context))
        {
            return;
        }

        var properties = (uniform is not null ? [uniform] : edits.Values).SelectMany(edit => edit.Ids).ToHashSet();
        if (this.active is null && this.supersededProperties?.Overlaps(properties) == true)
        {
            refresh();
            return;
        }

        if (this.active is { Properties: { } prior } && !prior.SetEquals(properties))
        {
            this.End(NumberBoxEditCompletionKind.Commit);
        }

        var gesture = this.active;
        var ticket = this.Diagnostics.Begin(properties);
        if (gesture is not null)
        {
            gesture.Properties ??= properties;
            this.Track(this.ObserveAsync(this.Send(gesture.Context, edits, uniform, gesture.Token), gesture.Context, gesture.Targets, ticket));
        }
        else
        {
            this.Track(this.ObserveAsync(this.Send(context, edits, uniform, EditSessionToken.OneShot), context, this.targets, ticket));
        }
    }

    // The scene environment is edited through its own command; nodes uniformly or per target.
    private Task<SceneCommandResult> Send(
        SceneDocumentCommandContext context,
        IReadOnlyDictionary<NodeId, PropertyEdit> edits,
        PropertyEdit? uniform,
        EditSessionToken token)
        => environment
            ? service.EditSceneEnvironmentPropertiesAsync(context, uniform ?? PropertyEdit.Empty, label, token)
            : token.IsOneShot && uniform is not null && edits.Count > 0
                ? service.EditPropertiesAsync(context, [.. edits.Keys], uniform, label, token)
                : service.EditPropertiesForTargetsAsync(context, edits, label, token);

    private async Task CommitAfterIdleAsync(Gesture gesture, CancellationTokenSource idle)
    {
        try
        {
            // A new wheel sample, selection change or explicit end supersedes this timer; it then
            // ends quietly instead of throwing per wheel tick.
            await Task.Delay(CommitGroupController.DefaultWheelIdleDelay, idle.Token).ConfigureAwait(ConfigureAwaitOptions.ContinueOnCapturedContext | ConfigureAwaitOptions.SuppressThrowing);
            if (!idle.IsCancellationRequested && ReferenceEquals(this.active, gesture) && ReferenceEquals(gesture.Idle, idle))
            {
                this.End(NumberBoxEditCompletionKind.Commit);
            }
        }
        finally
        {
            if (ReferenceEquals(gesture.Idle, idle))
            {
                gesture.Idle = null;
            }

            idle.Dispose();
        }
    }

    private async Task ObserveAsync(Task<SceneCommandResult> operation, SceneDocumentCommandContext context, IReadOnlyList<NodeId> editedTargets, InspectorFieldDiagnostics.Ticket? ticket)
    {
        var result = SceneCommandResult.Success;
        try
        {
            result = await operation.ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            result = new(Succeeded: false) { ValidationMessage = "The edit was cancelled." };
        }
        catch (InvalidOperationException exception)
        {
            result = new(Succeeded: false) { ValidationMessage = exception.Message };
        }
        finally
        {
            if (!this.disposed && this.IsBoundTo(context) && this.targets.ToHashSet().SetEquals(editedTargets))
            {
                this.refreshingResult = true;
                try
                {
                    refresh();
                }
                finally
                {
                    this.refreshingResult = false;
                }

                if (ticket is not null)
                {
                    this.Diagnostics.Complete(ticket, result);
                }
            }
        }
    }

    private bool IsBoundTo(SceneDocumentCommandContext? context)
        => ReferenceEquals(this.boundContext?.Scene, context?.Scene)
            && this.boundContext?.DocumentId == context?.DocumentId;

    private void Track(Task operation)
    {
        _ = this.pending.RemoveAll(task => task.IsCompleted);
        this.pending.Add(operation);
    }

    private async Task DrainAsync()
    {
        while (this.pending.Count > 0)
        {
            await Task.WhenAll(this.pending.ToArray()).ConfigureAwait(true);
            _ = this.pending.RemoveAll(task => task.IsCompleted);
        }
    }

    private sealed class Gesture(SceneDocumentCommandContext context, IReadOnlyList<NodeId> targets, string field, NumberBoxEditInteractionKind interaction, EditSessionToken token)
    {
        public SceneDocumentCommandContext Context { get; } = context;

        public IReadOnlyList<NodeId> Targets { get; } = targets.ToArray();

        public string Field { get; } = field;

        public NumberBoxEditInteractionKind Interaction { get; } = interaction;

        public EditSessionToken Token { get; } = token;

        public HashSet<PropertyId>? Properties { get; set; }

        public CancellationTokenSource? Idle { get; set; }
    }
}
