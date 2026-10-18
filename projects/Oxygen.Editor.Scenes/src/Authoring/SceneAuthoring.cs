// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>Executes every change to one open scene document: edits, undo and redo, and gestures.</summary>
/// <remarks>
/// <para>
/// Every public call runs on the scene thread: the synchronization context current when the document decoded, to which
/// calls from other threads are posted. Without one, calls run on the caller's thread, and the caller keeps them
/// serialized. Validation and application are synchronous on that thread, so edits never interleave.
/// </para>
/// <para>
/// The authoring records into the history it was given with the document and never creates, finds or clears one.
/// Disposing it retires it: every later call is refused.
/// </para>
/// </remarks>
public sealed partial class SceneAuthoring : IDisposable
{
    private readonly HistoryKeeper history;
    private readonly SynchronizationContext? context;
    private readonly SceneEditScope scope;

    // What the last replay reported, read back by undo and redo once the history has run the entry.
    private SceneEditResult? replayed;

    /// <summary>Initializes a new instance of the <see cref="SceneAuthoring"/> class.</summary>
    /// <param name="document">The document's identity and history.</param>
    /// <param name="scene">The document's scene.</param>
    /// <param name="layout">The scene's explorer layout, with an entry for every node.</param>
    /// <param name="interaction">The nodes hidden and locked in the editor, if the editor keeps them.</param>
    /// <param name="redirects">The session's asset renames and moves, if the editor tracks them.</param>
    internal SceneAuthoring(DocumentDecodeContext document, Scene scene, IList<ExplorerEntryData> layout, ISceneNodeInteraction? interaction, IAssetRedirects? redirects)
    {
        this.Document = document.Document;
        this.history = document.History;
        this.Scene = scene;
        this.Layout = layout;
        this.context = SynchronizationContext.Current;
        this.scope = new SceneEditScope(scene, layout, interaction, redirects);
    }

    /// <summary>Occurs on the scene thread after each edit, replay, preview and relocation, naming what changed.</summary>
    public event EventHandler<SceneChangedEventArgs>? Changed;

    /// <summary>Gets the scene's document.</summary>
    public DocumentId Document { get; }

    /// <summary>Gets the scene, for views and queries to read; only edits change it.</summary>
    public Scene Scene { get; }

    /// <summary>Gets the scene's explorer layout, for views and queries to read; only edits change it.</summary>
    /// <remarks>The layout is editor-only: it is saved in the scene file, and the engine never sees it.</remarks>
    public IList<ExplorerEntryData> Layout { get; }

    /// <summary>Gets a value indicating whether the authoring is retired: the scene was closed or replaced by a reload.</summary>
    public bool IsRetired { get; private set; }

    /// <summary>Executes an edit: checks its targets, validates it, applies it, records its revert and raises the changes.</summary>
    /// <param name="edit">The edit.</param>
    /// <returns>What became of the edit.</returns>
    public Task<SceneEditResult> ExecuteAsync(ISceneEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        return this.RunAsync(() => this.Execute(edit));
    }

    /// <summary>Checks whether an edit would apply to the scene as it is, without changing it.</summary>
    /// <param name="edit">The edit.</param>
    /// <returns>The validation.</returns>
    public Task<SceneEditValidation> ValidateAsync(ISceneEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        return this.RunAsync(() => this.Validate(edit));
    }

    /// <summary>Undoes the last edit.</summary>
    /// <returns>
    /// Applied, with any warning such as a restored reference to a deleted asset; Invalid [Edit.Empty] when there is
    /// nothing to undo; Refused when the scene changed in a way the entry cannot undo, which leaves the entry in place.
    /// </returns>
    public Task<SceneEditResult> UndoAsync() => this.RunAsync(() => this.Step(undo: true));

    /// <summary>Redoes the last undone edit.</summary>
    /// <returns>As for <see cref="UndoAsync"/>, for the last undone edit.</returns>
    public Task<SceneEditResult> RedoAsync() => this.RunAsync(() => this.Step(undo: false));

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.IsRetired)
        {
            return;
        }

        // Open gestures restore where they began while the authoring can still change the scene.
        this.CancelGestures();
        this.IsRetired = true;
    }

    /// <summary>Replays a history entry's edit: validates it, applies it, records its revert as the inverse and raises the changes.</summary>
    /// <param name="edit">The edit.</param>
    /// <param name="label">The name of the entry's edit, which the recorded inverse keeps.</param>
    /// <exception cref="SceneReplayRefusedException">The authoring is retired, or the edit no longer fits the scene.</exception>
    /// <remarks>
    /// The targets must still exist, but locks are not checked: they protect nodes from new authoring, and a replay returns
    /// the scene to a state the user already made.
    /// </remarks>
    internal void Replay(ISceneEdit edit, string label)
    {
        if (this.IsRetired)
        {
            throw this.Refuse(SceneEditResult.Retired);
        }

        this.scope.Replaying = true;
        try
        {
            this.ReplayChecked(edit, label);
        }
        finally
        {
            this.scope.Replaying = false;
        }
    }

    /// <summary>Runs a change made outside the history, such as a relocation, on the scene thread and raises its changes.</summary>
    /// <param name="change">The change; it returns what it changed.</param>
    /// <returns>A task that completes when the change ran.</returns>
    internal Task ChangeOutsideHistoryAsync(Func<Scene, IReadOnlyList<SceneChange>> change)
        => this.RunAsync(() =>
        {
            if (!this.IsRetired)
            {
                this.Raise(change(this.Scene));
            }

            return true;
        });

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the exception is forwarded to the caller's task")]
    internal Task<T> RunAsync<T>(Func<T> work)
    {
        if (this.context is null || ReferenceEquals(SynchronizationContext.Current, this.context))
        {
            return Task.FromResult(work());
        }

        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.context.Post(
            _ =>
            {
                try
                {
                    completion.SetResult(work());
                }
                catch (Exception exception)
                {
                    completion.SetException(exception);
                }
            },
            state: null);
        return completion.Task;
    }

    private void ReplayChecked(ISceneEdit edit, string label)
    {
        var validation = this.scope.CheckTargets(edit);
        if (validation.IsValid && edit is not SceneEditBatch)
        {
            validation = edit.Validate(this.scope);
        }

        if (!validation.IsValid)
        {
            _ = this.scope.TakeWarnings();
            throw this.Refuse(SceneEditResult.Of(validation));
        }

        switch (edit.Apply(this.scope))
        {
            case SceneEditApplication.Applied applied:
                this.history.AddChange(new SceneHistoryEntry(this, applied.Revert, label));
                this.replayed = new SceneEditResult(SceneEditOutcome.Applied, [.. validation.Diagnostics, .. this.scope.TakeWarnings()]);
                this.Raise(applied.Changes);
                break;
            case SceneEditApplication.Rejected rejected:
                _ = this.scope.TakeWarnings();
                throw this.Refuse(SceneEditResult.Of(rejected.Validation));
            default:
                throw new InvalidOperationException($"'{edit.Label}' returned an unknown application.");
        }
    }

    private SceneReplayRefusedException Refuse(SceneEditResult result)
    {
        this.replayed = result;
        return new SceneReplayRefusedException(result);
    }

    // The history runs the entry; the entry's replay leaves its result behind for the caller.
    private SceneEditResult Step(bool undo)
    {
        if (this.IsRetired)
        {
            return SceneEditResult.Retired;
        }

        if (!(undo ? this.history.CanUndo : this.history.CanRedo))
        {
            return SceneEditResult.Of(SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, undo ? "There is nothing to undo." : "There is nothing to redo.")));
        }

        this.replayed = null;
        _ = undo ? this.history.Undo() : this.history.Redo();
        var result = this.replayed ?? new SceneEditResult(SceneEditOutcome.Applied, []);
        this.replayed = null;
        return result;
    }

    private SceneEditResult Execute(ISceneEdit edit)
    {
        if (this.IsRetired)
        {
            return SceneEditResult.Retired;
        }

        var validation = this.CheckAndValidate(edit);
        if (!validation.IsValid)
        {
            _ = this.scope.TakeWarnings();
            return SceneEditResult.Of(validation);
        }

        this.scope.Warn(validation.Diagnostics);
        switch (edit.Apply(this.scope))
        {
            case SceneEditApplication.Applied { Changes.Count: 0 }:
                // The scene already was as the edit asks: nothing to record or to tell.
                return new SceneEditResult(SceneEditOutcome.Applied, this.scope.TakeWarnings());
            case SceneEditApplication.Applied applied:
                this.history.AddChange(new SceneHistoryEntry(this, applied.Revert, edit.Label));
                this.Raise(applied.Changes);
                return new SceneEditResult(SceneEditOutcome.Applied, this.scope.TakeWarnings());
            case SceneEditApplication.Rejected rejected:
                _ = this.scope.TakeWarnings();
                return SceneEditResult.Of(rejected.Validation);
            default:
                throw new InvalidOperationException($"'{edit.Label}' returned an unknown application.");
        }
    }

    private SceneEditValidation Validate(ISceneEdit edit)
    {
        if (this.IsRetired)
        {
            return SceneEditValidation.Refused(SceneEditResult.Retired.Diagnostics);
        }

        var validation = this.CheckAndValidate(edit);
        if (validation.IsValid && edit is SceneEditBatch)
        {
            validation = edit.Validate(this.scope);
        }

        _ = this.scope.TakeWarnings();
        return validation;
    }

    // A batch is not validated before it applies: it validates each inner edit and rolls back.
    private SceneEditValidation CheckAndValidate(ISceneEdit edit)
    {
        var validation = this.scope.CheckTargets(edit);
        return validation.IsValid && edit is not SceneEditBatch ? edit.Validate(this.scope) : validation;
    }

    private void Raise(IReadOnlyList<SceneChange> changes)
    {
        if (changes.Count != 0)
        {
            this.Changed?.Invoke(this, new SceneChangedEventArgs(this.Document, changes));
        }
    }
}
