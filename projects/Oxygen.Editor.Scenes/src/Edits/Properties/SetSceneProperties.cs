// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>Writes values to the scene's own properties: its environment.</summary>
/// <remarks>
/// The revert restores exactly the values the edit replaced, including those another property rewrote, as the backdrop
/// rewrites the atmosphere, the sky sphere and the background. References to moved assets follow the asset.
/// </remarks>
/// <param name="Edit">The values to write.</param>
/// <param name="Label">The edit's name in the history.</param>
public sealed record SetSceneProperties(PropertyEdit Edit, string Label) : ISceneEdit
{
    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Edit.Count == 0)
        {
            return SceneEditValidation.Valid;
        }

        return SceneProperties.RulesOf([this.Edit]) is not { IsSceneKind: true } rules
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, "A scene property edit must target the scene's environment."))
            : PropertyWrites.ToValidation(PropertyWrites.CheckValues(rules, this.Edit) ?? rules.Validate(scope.Scene, [new PropertyTargetEdit(Node: null, this.Edit)]));
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Edit.Count == 0)
        {
            return new SceneEditApplication.Applied(this, []);
        }

        var rules = SceneProperties.RulesOf([this.Edit])!;
        var target = (SceneEnvironmentTarget)rules.TargetOf(scope.Scene, node: null)!;
        var environment = scope.Scene.Environment;
        var before = PropertyWrites.Read(rules, target, rules.Descriptors.Keys);
        PropertyApply.ApplyToTarget(target, this.Edit, rules.Descriptors);
        if (SceneReferenceRetargeting.Retarget(target.Value, scope.Moved) is { } retargeted)
        {
            target.Value = retargeted;
        }

        scope.WarnIfDeleted(SceneReferenceRetargeting.ReferencedUris(target.Value));
        rules.Commit(scope.Scene, target);
        var after = PropertyWrites.Read(rules, target, rules.Descriptors.Keys);
        var replaced = PropertyEdit.Of(before.Where(entry => !after.TryGetRaw(entry.Key, out var value) || !Equals(entry.Value, value)));
        return replaced.Count == 0
            ? new SceneEditApplication.Applied(this, [])
            : new SceneEditApplication.Applied(new SetSceneEnvironment(environment, this.Label), [new SceneChange.PropertiesChanged(Node: null, [.. replaced.Ids], EditPhase.Commit)]);
    }
}
