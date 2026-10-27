// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
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
    public bool HasEditsInProgress => this.gestures.Exists(static gesture => gesture.HasChanges());

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
        return this.RunAsync(() => this.BeginGesture(targets, [], properties, label));
    }

    /// <summary>
    /// Begins a gesture on nodes of the scene and nodes an instance brings in: checks them and their locks, and captures
    /// their state; it writes nothing.
    /// </summary>
    /// <param name="nodes">The edited nodes; at least one.</param>
    /// <param name="properties">The edited properties, all of one component kind; changeable on an instanced node.</param>
    /// <param name="label">The gesture's name in the history.</param>
    /// <returns>The gesture, previewed with <see cref="SceneGesture.PreviewAsync(IReadOnlyDictionary{NodeReference, PropertyEdit})"/>.</returns>
    /// <exception cref="SceneEditRefusedException">
    /// The authoring is retired, a node is missing or locked, a node an instance brings in is not below an instance root,
    /// or the properties do not fit.
    /// </exception>
    public Task<SceneGesture> BeginNodeGestureAsync(IReadOnlyCollection<NodeReference> nodes, IReadOnlyCollection<PropertyId> properties, string label)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(properties);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        if (nodes.Count == 0)
        {
            throw new ArgumentException("A node gesture edits at least one node.", nameof(nodes));
        }

        NodeId[] owned = [.. nodes.Where(static node => node.IsOwned).Select(static node => node.Node)];
        NodeReference[] instanced = [.. nodes.Where(static node => !node.IsOwned).Distinct()];
        return this.RunAsync(() => this.BeginGesture(owned, instanced, properties, label));
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

    private SceneGesture BeginGesture(IReadOnlyCollection<NodeId> targets, NodeReference[] instanced, IReadOnlyCollection<PropertyId> properties, string label)
    {
        if (this.IsRetired)
        {
            throw new SceneEditRefusedException(SceneEditResult.Retired);
        }

        var rules = SceneProperties.RulesOf(properties);
        if (rules is null
            || rules.IsSceneKind != (targets.Count == 0 && instanced.Length == 0)
            || properties.Any(id => !rules.Descriptors.ContainsKey(id))
            || (instanced.Length != 0 && properties.Any(id => !SceneSchema.Changeable.ContainsKey(id))))
        {
            throw new SceneEditRefusedException(SceneEditResult.Of(SceneEditValidation.Invalid(
                SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, "A gesture edits properties of one supported kind, on nodes or on the scene."))));
        }

        var check = this.scope.CheckTargets(new GestureTargets(targets, instanced, label));
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

        var instances = new Dictionary<NodeReference, SceneInstanceComponent>();
        foreach (var node in instanced)
        {
            instances[node] = InstanceChangeWrites.InstanceOf(this.scope.FindNode(node.Node))
                ?? throw new SceneEditRefusedException(SceneEditResult.Of(SceneEditValidation.Invalid(
                    SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it brings in no node to change.", node.Node))));
        }

        var gesture = new SceneGesture(this, rules, properties.ToHashSet(), models, instances, label);
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
    internal sealed record GestureTargets(IReadOnlyCollection<NodeId> Targets, IReadOnlyCollection<NodeReference> InstancedTargets, string Label) : ISceneEdit
    {
        public SceneEditValidation Validate(SceneEditScope scope) => SceneEditValidation.Valid;

        public SceneEditApplication Apply(SceneEditScope scope) => throw new NotSupportedException("A gesture's targets are only checked.");
    }
}
