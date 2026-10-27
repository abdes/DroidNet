// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>A property edit that previews many times and records once, such as a drag or a slider.</summary>
/// <remarks>
/// <para>
/// The gesture captured the state of its targets for its properties, and those coupled to them, when it began; it wrote
/// nothing. Each preview validates and writes values and records nothing. The commit records one entry whose revert
/// restores the captured state; a commit that ends where the gesture began records nothing. A cancel restores the
/// captured state.
/// </para>
/// <para>
/// When a target is removed or replaced while the gesture is open, as by an undo in another view, the gesture is
/// cancelled at its next call and that call reports it.
/// </para>
/// <para>
/// A node an instance brings in is edited through its instance root's changes: a preview sets them, and the entry the
/// commit records restores them as <see cref="SetOverrides"/> does. Replacing the root's instance component replaces the
/// target.
/// </para>
/// </remarks>
public sealed class SceneGesture
{
    private readonly SceneAuthoring authoring;
    private readonly IComponentRules rules;
    private readonly IReadOnlySet<PropertyId> properties;
    private readonly IReadOnlySet<PropertyId> captured;
    private readonly Dictionary<NodeId, (object Target, PropertyEdit Before)> nodes;
    private readonly Dictionary<NodeReference, (SceneInstanceComponent Instance, SourceOverrides Before)> instanced;
    private readonly PropertyEdit sceneBefore;
    private readonly SceneEnvironmentData environmentBefore;

    /// <summary>Initializes a new instance of the <see cref="SceneGesture"/> class.</summary>
    /// <param name="authoring">The scene's authoring.</param>
    /// <param name="rules">The rules of the edited properties' kind.</param>
    /// <param name="properties">The properties the gesture edits.</param>
    /// <param name="targets">The edited nodes and their targets; empty for the scene's own properties.</param>
    /// <param name="instanced">The edited nodes an instance brings in, with their instance roots' components.</param>
    /// <param name="label">The gesture's name in the history.</param>
    internal SceneGesture(
        SceneAuthoring authoring,
        IComponentRules rules,
        IReadOnlySet<PropertyId> properties,
        IReadOnlyDictionary<NodeId, object> targets,
        IReadOnlyDictionary<NodeReference, SceneInstanceComponent> instanced,
        string label)
    {
        this.authoring = authoring;
        this.rules = rules;
        this.properties = properties;
        this.captured = rules.Coupled(properties);
        this.Label = label;
        this.nodes = targets.ToDictionary(static pair => pair.Key, pair => (pair.Value, PropertyWrites.Read(rules, pair.Value, this.captured)));
        this.instanced = instanced.ToDictionary(static pair => pair.Key, pair => (pair.Value, pair.Value.Changes.Of(pair.Key.Path).Of(rules.Kind)));
        this.sceneBefore = rules.IsSceneKind
            ? PropertyWrites.Read(rules, rules.TargetOf(authoring.Scene, node: null)!, this.captured)
            : PropertyEdit.Empty;
        this.environmentBefore = authoring.Scene.Environment;
    }

    /// <summary>Gets the gesture's name in the history.</summary>
    public string Label { get; }

    /// <summary>Gets a value indicating whether the gesture was committed or cancelled.</summary>
    public bool IsEnded { get; private set; }

    /// <summary>Writes preview values to the gesture's nodes.</summary>
    /// <param name="values">The values, for the gesture's nodes and properties only.</param>
    /// <returns>What became of the preview.</returns>
    public Task<SceneEditResult> PreviewAsync(SetProperties values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return this.authoring.RunAsync(() => this.Preview(values));
    }

    /// <summary>Writes preview values to the gesture's nodes: nodes of the scene and nodes an instance brings in.</summary>
    /// <param name="values">The values of each of the gesture's nodes, for its properties only.</param>
    /// <returns>What became of the preview.</returns>
    public Task<SceneEditResult> PreviewAsync(IReadOnlyDictionary<NodeReference, PropertyEdit> values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return this.authoring.RunAsync(() => this.Preview(values));
    }

    /// <summary>Writes preview values to the scene's own properties.</summary>
    /// <param name="values">The values, for the gesture's properties only.</param>
    /// <returns>What became of the preview.</returns>
    public Task<SceneEditResult> PreviewAsync(SetSceneProperties values)
    {
        ArgumentNullException.ThrowIfNull(values);
        return this.authoring.RunAsync(() => this.Preview(values));
    }

    /// <summary>Ends the gesture with one history entry from where it began to its last preview.</summary>
    /// <returns>What became of the gesture.</returns>
    public Task<SceneEditResult> CommitAsync() => this.authoring.RunAsync(this.Commit);

    /// <summary>Ends the gesture where it began, with no history entry.</summary>
    /// <returns>A task that completes when the gesture's state is restored.</returns>
    public Task CancelAsync() => this.authoring.RunAsync(() =>
    {
        this.Cancel();
        return true;
    });

    /// <summary>Gets a value indicating whether the gesture's current values differ from where it began.</summary>
    /// <returns>Whether a commit now would record an entry.</returns>
    internal bool HasChanges()
        => !this.IsEnded && !this.TargetsChanged() && (this.Replaced().Any(static pair => pair.Value.Count > 0) || this.InstancedRestores().Count > 0);

    /// <summary>Commits the gesture.</summary>
    /// <returns>What became of the gesture.</returns>
    internal SceneEditResult Commit()
    {
        if (this.Ended() is { } ended)
        {
            return ended;
        }

        var replaced = this.Replaced();
        var restores = this.InstancedRestores();
        this.End();
        if (replaced.All(static pair => pair.Value.Count == 0) && restores.Count == 0)
        {
            // Nothing to record, but the engine may still show a throttled preview: the commit settles it.
            this.authoring.RaiseChanges(this.Changes(EditPhase.Commit));
            return new SceneEditResult(SceneEditOutcome.Applied, []);
        }

        var revert = this.rules.IsSceneKind
            ? new SetSceneEnvironment(this.environmentBefore, this.Label)
            : this.Revert(replaced.Where(static pair => pair.Value.Count > 0).ToDictionary(), restores);
        this.authoring.Record(revert, this.Changes(EditPhase.Commit), this.Label);
        return new SceneEditResult(SceneEditOutcome.Applied, []);
    }

    /// <summary>Cancels the gesture: restores where it began.</summary>
    internal void Cancel()
    {
        if (this.IsEnded)
        {
            return;
        }

        this.End();
        if (this.authoring.IsRetired)
        {
            return;
        }

        if (this.rules.IsSceneKind)
        {
            // Restoring the descriptor values would miss the records only the backdrop rewrites.
            this.authoring.Scene.SetEnvironment(this.environmentBefore);
        }
        else
        {
            foreach (var (node, (target, before)) in this.nodes)
            {
                if (ReferenceEquals(this.rules.TargetOf(this.authoring.Scene, this.authoring.FindNode(node)), target))
                {
                    PropertyApply.ApplyToTarget(target, before, this.rules.Descriptors);
                }
            }

            foreach (var (reference, (instance, before)) in this.instanced)
            {
                if (!ReferenceEquals(InstanceChangeWrites.InstanceOf(this.authoring.FindNode(reference.Node)), instance))
                {
                    continue;
                }

                foreach (var property in this.captured)
                {
                    _ = InstanceChangeWrites.Write(instance, reference.Path, OverrideWrite.Restoring(property, part: null, before.Find(property, part: null)), out _);
                }
            }
        }

        this.authoring.RaiseChanges(this.Changes(EditPhase.Cancel));
    }

    private static PropertyEdit Differences(PropertyEdit before, PropertyEdit current)
        => PropertyEdit.Of(before.Where(entry => !current.TryGetRaw(entry.Key, out var value) || !Equals(entry.Value, value)));

    private SceneEditResult Preview(SetProperties values)
    {
        if (this.rules.IsSceneKind || !values.Edits.Keys.ToHashSet().SetEquals(this.nodes.Keys) || !this.EditsOnlyItsProperties(values.Edits.Values))
        {
            return SceneEditResult.Of(SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.GestureMismatch, "A preview must edit the gesture's own nodes and properties.")));
        }

        return this.PreviewChecked([values], () =>
        {
            foreach (var (node, edit) in values.Edits)
            {
                PropertyApply.ApplyToTarget(this.nodes[node].Target, edit, this.rules.Descriptors);
            }

            return [.. values.Edits.Select(pair => new SceneChange.PropertiesChanged(pair.Key, [.. pair.Value.Ids], EditPhase.Preview))];
        });
    }

    private SceneEditResult Preview(IReadOnlyDictionary<NodeReference, PropertyEdit> values)
    {
        var owned = values.Where(static pair => pair.Key.IsOwned).ToDictionary(static pair => pair.Key.Node, static pair => pair.Value);
        var changes = values.Where(static pair => !pair.Key.IsOwned).ToDictionary();
        if (this.rules.IsSceneKind
            || !owned.Keys.ToHashSet().SetEquals(this.nodes.Keys)
            || !changes.Keys.ToHashSet().SetEquals(this.instanced.Keys)
            || !this.EditsOnlyItsProperties(values.Values))
        {
            return SceneEditResult.Of(SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.GestureMismatch, "A preview must edit the gesture's own nodes and properties.")));
        }

        var writes = NodeOverrides.ChangeWrites(changes);
        List<ISceneEdit> checks = [];
        if (owned.Count > 0)
        {
            checks.Add(new SetProperties(owned, this.Label));
        }

        if (writes.Count > 0)
        {
            checks.Add(new SetOverrides(writes, this.Label));
        }

        return this.PreviewChecked(checks, () =>
        {
            foreach (var (node, edit) in owned)
            {
                PropertyApply.ApplyToTarget(this.nodes[node].Target, edit, this.rules.Descriptors);
            }

            List<SceneChange> raised = [.. owned.Select(static pair => new SceneChange.PropertiesChanged(pair.Key, [.. pair.Value.Ids], EditPhase.Preview))];
            foreach (var (root, rootWrites) in writes)
            {
                foreach (var write in rootWrites)
                {
                    var resolved = write.Value is AssetPath asset ? write with { Value = this.authoring.Scope.Resolve(asset) } : write;
                    _ = InstanceChangeWrites.Write(this.instanced[new NodeReference(root, write.Path!)].Instance, write.Path!, resolved, out _);
                }
            }

            raised.AddRange(writes.Select(static pair => new SceneChange.OverridesChanged(pair.Key, [.. pair.Value.Select(static write => write.Property).Distinct()])));
            return raised;
        });
    }

    private SceneEditResult Preview(SetSceneProperties values)
    {
        if (!this.rules.IsSceneKind || !this.EditsOnlyItsProperties([values.Edit]))
        {
            return SceneEditResult.Of(SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.GestureMismatch, "A preview must edit the gesture's own properties.")));
        }

        return this.PreviewChecked([values], () =>
        {
            var target = this.rules.TargetOf(this.authoring.Scene, node: null)!;
            PropertyApply.ApplyToTarget(target, values.Edit, this.rules.Descriptors);
            this.rules.Commit(this.authoring.Scene, target);
            return [new SceneChange.PropertiesChanged(Node: null, [.. values.Edit.Ids], EditPhase.Preview)];
        });
    }

    private SceneEditResult PreviewChecked(IReadOnlyList<ISceneEdit> checks, Func<IReadOnlyList<SceneChange>> write)
    {
        if (this.Ended() is { } ended)
        {
            return ended;
        }

        foreach (var check in checks)
        {
            var validation = check.Validate(this.authoring.Scope);
            if (!validation.IsValid)
            {
                return SceneEditResult.Of(validation);
            }
        }

        this.authoring.RaiseChanges(write());
        return new SceneEditResult(SceneEditOutcome.Applied, []);
    }

    // Reports why the gesture cannot go on, cancelling it when a target went away; null when it can.
    private SceneEditResult? Ended()
    {
        if (this.IsEnded)
        {
            return SceneEditResult.Of(SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.GestureMismatch, "The gesture has ended.")));
        }

        if (this.authoring.IsRetired)
        {
            this.End();
            return SceneEditResult.Retired;
        }

        if (this.TargetsChanged())
        {
            this.Cancel();
            return SceneEditResult.Of(SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.GestureCancelled, "An edited component was removed or replaced, so the gesture was cancelled.")));
        }

        return null;
    }

    private bool EditsOnlyItsProperties(IEnumerable<PropertyEdit> edits) => edits.All(edit => edit.Ids.All(this.properties.Contains));

    private bool TargetsChanged()
        => this.nodes.Any(pair => !ReferenceEquals(this.rules.TargetOf(this.authoring.Scene, this.authoring.FindNode(pair.Key)), pair.Value.Target))
            || this.instanced.Any(pair => !ReferenceEquals(InstanceChangeWrites.InstanceOf(this.authoring.FindNode(pair.Key.Node)), pair.Value.Instance));

    // The writes that put back the captured changes of the nodes an instance brings in, by instance root.
    private Dictionary<NodeId, IReadOnlyList<OverrideWrite>> InstancedRestores()
    {
        var restores = new Dictionary<NodeId, List<OverrideWrite>>();
        foreach (var (reference, (instance, before)) in this.instanced)
        {
            var current = instance.Changes.Of(reference.Path).Of(this.rules.Kind);
            foreach (var property in this.captured)
            {
                var previous = before.Find(property, part: null);
                if (Equals(previous, current.Find(property, part: null)))
                {
                    continue;
                }

                if (!restores.TryGetValue(reference.Node, out var writes))
                {
                    restores[reference.Node] = writes = [];
                }

                writes.Add(OverrideWrite.Restoring(property, part: null, previous) with { Path = reference.Path });
            }
        }

        return restores.ToDictionary(static pair => pair.Key, static pair => (IReadOnlyList<OverrideWrite>)pair.Value);
    }

    // The entry that restores where the gesture began: the scene's nodes' values, the instance roots' changes, or both.
    private ISceneEdit Revert(Dictionary<NodeId, PropertyEdit> replaced, Dictionary<NodeId, IReadOnlyList<OverrideWrite>> restores)
    {
        if (restores.Count == 0)
        {
            return new SetProperties(replaced, this.Label);
        }

        var changes = new SetOverrides(restores, this.Label);
        return replaced.Count == 0 ? changes : new SceneEditBatch(this.Label, [new SetProperties(replaced, this.Label), changes]);
    }

    // The captured values each target no longer has; the scene's own properties are keyed by the empty id.
    private Dictionary<NodeId, PropertyEdit> Replaced()
    {
        if (this.rules.IsSceneKind)
        {
            var current = PropertyWrites.Read(this.rules, this.rules.TargetOf(this.authoring.Scene, node: null)!, this.captured);
            return new() { [NodeId.Empty] = Differences(this.sceneBefore, current) };
        }

        return this.nodes.ToDictionary(
            static pair => pair.Key,
            pair => Differences(pair.Value.Before, PropertyWrites.Read(this.rules, pair.Value.Target, this.captured)));
    }

    private IReadOnlyList<SceneChange> Changes(EditPhase phase)
        => this.rules.IsSceneKind
            ? [new SceneChange.PropertiesChanged(Node: null, [.. this.captured], phase)]
            : [
                .. this.nodes.Keys.Select(node => new SceneChange.PropertiesChanged(node, [.. this.captured], phase)),
                .. this.instanced.Keys.Select(static reference => reference.Node).Distinct().Select(root => new SceneChange.OverridesChanged(root, [.. this.captured])),
            ];

    private void End()
    {
        this.IsEnded = true;
        this.authoring.Forget(this);
    }
}
