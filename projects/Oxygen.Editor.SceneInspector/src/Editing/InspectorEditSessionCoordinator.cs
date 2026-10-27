// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.SceneInspector.Components.Instances;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>Captures control targets and runs each control gesture as one scene gesture: previews, then one commit.</summary>
/// <remarks>
/// A target is a node of the scene or a node an instance brings in, named by its reference: the edits of the latter land in
/// its instance root's changes (scene instances 8.5), so a section never knows which it edits.
/// </remarks>
internal sealed partial class InspectorEditSessionCoordinator(
    Func<SceneAuthoring?> authoringProvider,
    string label,
    Action refresh,
    bool environment = false,
    InspectorFieldDiagnostics? diagnostics = null,
    Func<InspectorSourceApply?>? sourceApply = null) : IDisposable
{
    private const string ResetLabel = "Reset to Source";
    private static readonly TimeSpan WheelIdleDelay = TimeSpan.FromMilliseconds(250);
    private static readonly SceneEditResult Nothing = new(SceneEditOutcome.Applied, []);
    private readonly List<Task> pending = [];

    // Node targets; in environment mode the target is the bound scene and this stays empty.
    private NodeReference[] targets = [];
    private InspectorFieldOverrides? overrides;
    private SceneAuthoring? boundAuthoring;
    private Gesture? active;
    private HashSet<PropertyId>? supersededProperties;
    private bool disposed;
    private bool refreshingResult;
    private bool inputEnabled = true;

    /// <summary>Gets the current diagnostics for the bound fields.</summary>
    public InspectorFieldDiagnostics Diagnostics { get; } = diagnostics ?? new();

    /// <summary>Gets the override bars of the bound fields, from what the targets override of their sources.</summary>
    public InspectorFieldOverrides Overrides => this.overrides ??= new(this.Reset, sourceApply);

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
    /// <param name="nodes">The bound targets.</param>
    public void Bind(IReadOnlyList<NodeReference> nodes)
    {
        var authoring = authoringProvider();
        this.Overrides.Update(authoring?.Scene, nodes);
        if (this.IsBoundTo(authoring) && this.targets.ToHashSet().SetEquals(nodes))
        {
            return;
        }

        this.supersededProperties = this.active?.Properties;
        var completion = this.active?.Interaction == NumberBoxEditInteractionKind.Text ? NumberBoxEditCompletionKind.Commit : NumberBoxEditCompletionKind.Cancel;

        // Cancellation changes the old model and can synchronously reenter Bind
        // through the inspector host. Publish the new scope before that callback.
        this.boundAuthoring = authoring;
        this.targets = [.. nodes];
        this.ScopeId = Guid.NewGuid();
        this.Diagnostics.Reset();
        this.End(completion);
    }

    /// <summary>Begins a control-owned gesture with stable document and target identities.</summary>
    /// <param name="field">The edited field key.</param>
    /// <param name="interaction">The control interaction that owns the gesture.</param>
    public void Begin(string field, NumberBoxEditInteractionKind interaction)
    {
        if (this.disposed || !this.inputEnabled || authoringProvider() is not { } authoring || (!environment && this.targets.Length == 0))
        {
            return;
        }

        if (this.active is { } current && string.Equals(current.Field, field, StringComparison.Ordinal))
        {
            current.Idle?.Cancel();
            return;
        }

        this.End(NumberBoxEditCompletionKind.Commit);
        this.supersededProperties = null;
        this.active = new(authoring, this.targets, field, interaction);
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
    /// <param name="edit">The uniform requested values, committed as <see cref="InspectorCommits"/> says.</param>
    public void Submit(PropertyEdit edit)
    {
        var committed = InspectorCommits.Commit(edit);
        this.Submit(this.targets.ToDictionary(id => id, _ => committed), uniform: committed);
    }

    /// <summary>Submits per-target values for color axes and world-space sun orientation.</summary>
    /// <param name="edits">The requested values for each target, committed as <see cref="InspectorCommits"/> says.</param>
    public void Submit(IReadOnlyDictionary<NodeReference, PropertyEdit> edits)
        => this.Submit(InspectorCommits.Commit(edits), uniform: null);

    /// <summary>Resets properties to the source's value on every target that overrides them, as one edit.</summary>
    /// <param name="properties">The properties.</param>
    public void Reset(IReadOnlyList<PropertyId> properties)
    {
        ArgumentNullException.ThrowIfNull(properties);
        if (this.disposed || !this.inputEnabled || authoringProvider() is not { } authoring || !this.IsBoundTo(authoring))
        {
            return;
        }

        var writes = this.Overrides.Overriding(properties).ToDictionary(
            static pair => pair.Key,
            static pair => (IReadOnlyList<OverrideWrite>)[.. pair.Value.Select(static property => new OverrideWrite(property, Part: null, LayoutRevision: null, Value: null))]);
        if (writes.Count == 0)
        {
            return;
        }

        this.End(NumberBoxEditCompletionKind.Commit);
        var ticket = this.Diagnostics.Begin(properties);
        this.Track(this.ObserveAsync(authoring.ExecuteAsync(NodeOverrides.Write(writes, ResetLabel)), authoring, this.targets, ticket));
    }

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
        gesture.Steps = FinishAsync(gesture.Steps, gesture.Scene, completion);
        this.Track(this.ObserveAsync(gesture.Steps, gesture.Authoring, gesture.Targets, ticket: null));
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

    // Each step of a gesture waits for the previous one, so the scene sees them in order.
    private static async Task<SceneEditResult> FinishAsync(Task<SceneEditResult> previous, Task<SceneGesture>? scene, NumberBoxEditCompletionKind completion)
    {
        _ = await previous.ConfigureAwait(true);
        if (scene is null)
        {
            return Nothing;
        }

        var gesture = await scene.ConfigureAwait(true);
        if (completion == NumberBoxEditCompletionKind.Cancel)
        {
            await gesture.CancelAsync().ConfigureAwait(true);
            return Nothing;
        }

        return await gesture.CommitAsync().ConfigureAwait(true);
    }

    private void Submit(IReadOnlyDictionary<NodeReference, PropertyEdit> edits, PropertyEdit? uniform)
    {
        if (this.disposed || !this.inputEnabled || authoringProvider() is not { } authoring || !this.IsBoundTo(authoring))
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
            // The scene gesture starts with the first value, when its properties are known.
            gesture.Properties ??= properties;
            gesture.Scene ??= environment
                ? authoring.BeginGestureAsync([], properties, label)
                : authoring.BeginNodeGestureAsync(gesture.Targets, properties, label);
            gesture.Steps = this.PreviewAsync(gesture.Steps, gesture.Scene, edits, uniform);
            this.Track(this.ObserveAsync(gesture.Steps, gesture.Authoring, gesture.Targets, ticket));
        }
        else
        {
            this.Track(this.ObserveAsync(authoring.ExecuteAsync(this.EditOf(edits, uniform)), authoring, this.targets, ticket));
        }
    }

    // The scene environment is edited as scene properties; nodes uniformly or per target.
    private ISceneEdit EditOf(IReadOnlyDictionary<NodeReference, PropertyEdit> edits, PropertyEdit? uniform)
        => environment ? new SetSceneProperties(uniform ?? PropertyEdit.Empty, label) : NodeOverrides.SetValues(edits, label);

    private async Task<SceneEditResult> PreviewAsync(Task<SceneEditResult> previous, Task<SceneGesture> scene, IReadOnlyDictionary<NodeReference, PropertyEdit> edits, PropertyEdit? uniform)
    {
        _ = await previous.ConfigureAwait(true);
        var gesture = await scene.ConfigureAwait(true);
        return environment
            ? await gesture.PreviewAsync(new SetSceneProperties(uniform ?? PropertyEdit.Empty, label)).ConfigureAwait(true)
            : await gesture.PreviewAsync(edits).ConfigureAwait(true);
    }

    private async Task CommitAfterIdleAsync(Gesture gesture, CancellationTokenSource idle)
    {
        try
        {
            // A new wheel sample, selection change or explicit end supersedes this timer; it then
            // ends quietly instead of throwing per wheel tick.
            await Task.Delay(WheelIdleDelay, idle.Token).ConfigureAwait(ConfigureAwaitOptions.ContinueOnCapturedContext | ConfigureAwaitOptions.SuppressThrowing);
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

    private async Task ObserveAsync(Task<SceneEditResult> operation, SceneAuthoring authoring, IReadOnlyList<NodeReference> editedTargets, InspectorFieldDiagnostics.Ticket? ticket)
    {
        var result = Nothing;
        try
        {
            result = await operation.ConfigureAwait(true);
        }
        catch (SceneEditRefusedException refused)
        {
            result = refused.Result ?? SceneEditResult.Retired;
        }
        finally
        {
            if (!this.disposed && this.IsBoundTo(authoring) && this.targets.ToHashSet().SetEquals(editedTargets))
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

    private bool IsBoundTo(SceneAuthoring? authoring) => ReferenceEquals(this.boundAuthoring, authoring);

    private void Track(Task operation)
    {
        _ = this.pending.RemoveAll(task => task.IsCompleted);
        this.pending.Add(operation);
    }

    private async Task DrainAsync()
    {
        while (this.pending.Count > 0)
        {
            await Task.WhenAll([.. this.pending]).ConfigureAwait(true);
            _ = this.pending.RemoveAll(task => task.IsCompleted);
        }
    }

    private sealed class Gesture(SceneAuthoring authoring, IReadOnlyList<NodeReference> targets, string field, NumberBoxEditInteractionKind interaction)
    {
        public SceneAuthoring Authoring { get; } = authoring;

        public IReadOnlyList<NodeReference> Targets { get; } = [.. targets];

        public string Field { get; } = field;

        public NumberBoxEditInteractionKind Interaction { get; } = interaction;

        public Task<SceneGesture>? Scene { get; set; }

        // The last step submitted to the scene gesture; later steps wait for it.
        public Task<SceneEditResult> Steps { get; set; } = Task.FromResult(Nothing);

        public HashSet<PropertyId>? Properties { get; set; }

        public CancellationTokenSource? Idle { get; set; }
    }
}
