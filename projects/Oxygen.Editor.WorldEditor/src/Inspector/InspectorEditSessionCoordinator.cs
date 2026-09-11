// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.WorldEditor.Documents.Commands;

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
    private Guid[] targets = [];
    private SceneDocumentCommandContext? boundContext;
    private Gesture? active;
    private HashSet<PropertyId>? supersededProperties;
    private bool disposed;
    private bool refreshingResult;

    /// <summary>Gets the current diagnostics for the bound fields.</summary>
    public InspectorFieldDiagnostics Diagnostics { get; } = diagnostics ?? new();

    /// <summary>Gets the identity of the current binding lifetime.</summary>
    public Guid ScopeId { get; private set; } = Guid.NewGuid();

    /// <summary>Gets completion of already submitted edits.</summary>
    public Task Pending => this.DrainAsync();

    /// <summary>Updates the binding scope, ending the old gesture first.</summary>
    /// <param name="nodeIds">The bound target identities.</param>
    public void Bind(IReadOnlyList<Guid> nodeIds)
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
        if (this.disposed || contextProvider() is not { } context || this.targets.Length == 0)
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
            EditSessionToken.Begin(environment ? "Scene.Environment.Edit" : "Scene.Property.Edit", this.targets, field));
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
        => this.Submit(this.targets.ToDictionary(id => id, _ => edit.Clone()), uniform: true);

    /// <summary>Submits per-target values for color axes and world-space sun orientation.</summary>
    /// <param name="edits">The requested values for each target.</param>
    public void Submit(IReadOnlyDictionary<Guid, PropertyEdit> edits)
        => this.Submit(edits, uniform: false);

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
        this.Track(this.ObserveAsync(service.EditPropertiesForTargetsAsync(gesture.Context, empty, label, gesture.Token), gesture.Context, gesture.Targets, ticket: null));
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

    private void Submit(IReadOnlyDictionary<Guid, PropertyEdit> edits, bool uniform)
    {
        if (this.disposed || contextProvider() is not { } context || !this.IsBoundTo(context))
        {
            return;
        }

        var properties = edits.Values.SelectMany(edit => edit.Ids).ToHashSet();
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
        var ticket = this.Diagnostics.Begin(properties, context.Metadata.ChangeVersion);
        if (gesture is not null)
        {
            gesture.Properties ??= properties;
            this.Track(this.ObserveAsync(service.EditPropertiesForTargetsAsync(gesture.Context, edits, label, gesture.Token), gesture.Context, gesture.Targets, ticket));
        }
        else if (uniform && edits.Count > 0)
        {
            var edit = edits.Values.First();
            var operation = environment
                ? service.EditSceneEnvironmentPropertiesAsync(context, edit, label, EditSessionToken.OneShot)
                : service.EditPropertiesAsync(context, edits.Keys.ToArray(), edit, label, EditSessionToken.OneShot);
            this.Track(this.ObserveAsync(operation, context, this.targets, ticket));
        }
        else
        {
            this.Track(this.ObserveAsync(service.EditPropertiesForTargetsAsync(context, edits, label, EditSessionToken.OneShot), context, this.targets, ticket));
        }
    }

    private async Task CommitAfterIdleAsync(Gesture gesture, CancellationTokenSource idle)
    {
        try
        {
            await Task.Delay(CommitGroupController.DefaultWheelIdleDelay, idle.Token).ConfigureAwait(true);
            if (ReferenceEquals(this.active, gesture) && ReferenceEquals(gesture.Idle, idle))
            {
                this.End(NumberBoxEditCompletionKind.Commit);
            }
        }
        catch (OperationCanceledException)
        {
            // A new wheel sample, selection change or explicit end superseded this timer.
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

    private async Task ObserveAsync(Task<SceneCommandResult> operation, SceneDocumentCommandContext context, IReadOnlyList<Guid> editedTargets, InspectorFieldDiagnostics.Ticket? ticket)
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
            && ReferenceEquals(this.boundContext?.Metadata, context?.Metadata)
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

    private sealed class Gesture(SceneDocumentCommandContext context, IReadOnlyList<Guid> targets, string field, NumberBoxEditInteractionKind interaction, EditSessionToken token)
    {
        public SceneDocumentCommandContext Context { get; } = context;

        public IReadOnlyList<Guid> Targets { get; } = targets.ToArray();

        public string Field { get; } = field;

        public NumberBoxEditInteractionKind Interaction { get; } = interaction;

        public EditSessionToken Token { get; } = token;

        public HashSet<PropertyId>? Properties { get; set; }

        public CancellationTokenSource? Idle { get; set; }
    }
}
