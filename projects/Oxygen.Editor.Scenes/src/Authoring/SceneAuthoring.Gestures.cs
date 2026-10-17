// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <content>The gestures: property edits that preview many times and record once.</content>
public sealed partial class SceneAuthoring
{
    private readonly List<SceneGesture> gestures = [];

    /// <summary>
    /// Gets a value indicating whether an open gesture shows the scene changed from where it began, so a save or a cook
    /// must wait for it; a drag returned to its start does not count.
    /// </summary>
    public bool HasEditsInProgress => this.gestures.Any(static gesture => gesture.HasChanges());

    /// <summary>Gets the scene, for the gestures and edits that run through this authoring.</summary>
    internal SceneEditScope Scope => this.scope;

    /// <summary>Begins a gesture: checks its targets and their locks, and captures their state; it writes nothing.</summary>
    /// <param name="targets">The edited nodes; empty for the scene's own properties.</param>
    /// <param name="properties">The edited properties, all of one component kind.</param>
    /// <param name="label">The gesture's name in the history.</param>
    /// <returns>The gesture.</returns>
    /// <exception cref="SceneEditRefusedException">The authoring is retired, a target is missing or locked, or the properties do not fit.</exception>
    public Task<SceneGesture> BeginGestureAsync(IReadOnlyCollection<NodeId> targets, IReadOnlyCollection<PropertyId> properties, string label)
    {
        ArgumentNullException.ThrowIfNull(targets);
        ArgumentNullException.ThrowIfNull(properties);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        return this.RunAsync(() => this.BeginGesture(targets, properties, label));
    }

    /// <summary>Commits every open gesture, each as one history entry.</summary>
    /// <returns>A task that completes when the gestures are committed.</returns>
    public Task FinishEditsAsync() => this.RunAsync(() =>
    {
        foreach (var gesture in this.gestures.ToArray())
        {
            _ = gesture.Commit();
        }

        return true;
    });

    /// <summary>Finds a node of the scene.</summary>
    /// <param name="id">The node's id.</param>
    /// <returns>The node, or <see langword="null"/>.</returns>
    internal SceneNode? FindNode(NodeId id) => this.scope.FindNode(id);

    /// <summary>Records a gesture's entry and raises its changes.</summary>
    /// <param name="revert">The edit that restores where the gesture began.</param>
    /// <param name="changes">What the gesture changed.</param>
    /// <param name="label">The gesture's name.</param>
    internal void Record(ISceneEdit revert, IReadOnlyList<SceneChange> changes, string label)
    {
        this.history.AddChange(new SceneHistoryEntry(this, revert, label));
        this.Raise(changes);
    }

    /// <summary>Raises changes made outside any recorded edit, such as a gesture's preview.</summary>
    /// <param name="changes">The changes.</param>
    internal void RaiseChanges(IReadOnlyList<SceneChange> changes) => this.Raise(changes);

    /// <summary>Stops tracking a gesture that ended.</summary>
    /// <param name="gesture">The gesture.</param>
    internal void Forget(SceneGesture gesture) => _ = this.gestures.Remove(gesture);

    private SceneGesture BeginGesture(IReadOnlyCollection<NodeId> targets, IReadOnlyCollection<PropertyId> properties, string label)
    {
        if (this.IsRetired)
        {
            throw new SceneEditRefusedException(SceneEditResult.Retired);
        }

        var rules = SceneProperties.RulesOf(properties);
        if (rules is null || rules.IsSceneKind != (targets.Count == 0) || properties.Any(id => !rules.Descriptors.ContainsKey(id)))
        {
            throw new SceneEditRefusedException(SceneEditResult.Of(SceneEditValidation.Invalid(
                SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, "A gesture edits properties of one supported kind, on nodes or on the scene."))));
        }

        var check = this.scope.CheckTargets(new GestureTargets(targets, label));
        if (!check.IsValid)
        {
            throw new SceneEditRefusedException(SceneEditResult.Of(check));
        }

        var models = new Dictionary<NodeId, object>();
        foreach (var node in targets.Distinct())
        {
            models[node] = rules.TargetOf(this.Scene, this.scope.FindNode(node))
                ?? throw new SceneEditRefusedException(SceneEditResult.Of(SceneEditValidation.Invalid(
                    SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no component for the edited properties.", node))));
        }

        var gesture = new SceneGesture(this, rules, properties.ToHashSet(), models, label);
        this.gestures.Add(gesture);
        return gesture;
    }

    private void CancelGestures()
    {
        foreach (var gesture in this.gestures.ToArray())
        {
            gesture.Cancel();
        }
    }

    // The targets of a gesture, checked once when it begins, the way an edit's targets are.
    private sealed record GestureTargets(IReadOnlyCollection<NodeId> Targets, string Label) : ISceneEdit
    {
        public SceneEditValidation Validate(SceneEditScope scope) => SceneEditValidation.Valid;

        public SceneEditApplication Apply(SceneEditScope scope) => throw new NotSupportedException("A gesture's targets are only checked.");
    }
}
