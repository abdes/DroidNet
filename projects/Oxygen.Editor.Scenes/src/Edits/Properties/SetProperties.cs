// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>Writes property values to the components of one kind on several nodes, each node its own values.</summary>
/// <remarks>
/// The revert restores exactly the values the edit replaced, coupled properties included: editing one rotation axis
/// restores all three.
/// </remarks>
public sealed record SetProperties : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetProperties"/> class.</summary>
    /// <param name="edits">The values to write, by node.</param>
    /// <param name="label">The edit's name in the history.</param>
    public SetProperties(IReadOnlyDictionary<NodeId, PropertyEdit> edits, string label)
    {
        ArgumentNullException.ThrowIfNull(edits);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        this.Edits = edits.ToFrozenDictionary();
        this.Label = label;
    }

    /// <summary>Gets the values to write, by node.</summary>
    public IReadOnlyDictionary<NodeId, PropertyEdit> Edits { get; }

    /// <inheritdoc/>
    public string Label { get; }

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [.. this.Edits.Keys];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var edits = this.Edits.Where(static pair => pair.Value.Count > 0).ToArray();
        if (edits.Length == 0)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No property to edit."));
        }

        if (SceneProperties.RulesOf(edits.Select(static pair => pair.Value)) is not { IsSceneKind: false } rules)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, "A property edit must target one supported component kind."));
        }

        foreach (var (node, edit) in edits)
        {
            if (rules.TargetOf(scope.Scene, scope.FindNode(node)) is null)
            {
                return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node has no component for the edited properties.", node));
            }

            if (PropertyWrites.CheckValues(rules, edit) is { } error)
            {
                return SceneEditValidation.Invalid(error with { Node = node });
            }
        }

        return PropertyWrites.ToValidation(rules.Validate(scope.Scene, [.. edits.Select(static pair => new PropertyTargetEdit(pair.Key, pair.Value))]));
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var edits = this.Edits.Where(static pair => pair.Value.Count > 0).ToArray();
        if (edits.Length == 0)
        {
            return new SceneEditApplication.Applied(this, []);
        }

        var rules = SceneProperties.RulesOf(edits.Select(static pair => pair.Value))!;
        var reverts = new Dictionary<NodeId, PropertyEdit>();
        var changes = new List<SceneChange>();
        foreach (var (node, edit) in edits)
        {
            var replaced = PropertyWrites.Write(scope.Scene, rules, rules.TargetOf(scope.Scene, scope.FindNode(node))!, edit);
            if (replaced.Count > 0)
            {
                reverts[node] = replaced;
                // Coupled properties are listed with the changed ones: normalizing one Euler axis can move the others' reading.
                changes.Add(new SceneChange.PropertiesChanged(node, [.. rules.Coupled(edit.Ids).Union(replaced.Ids)], EditPhase.Commit));
            }
        }

        return new SceneEditApplication.Applied(new SetProperties(reverts, this.Label), changes);
    }
}
