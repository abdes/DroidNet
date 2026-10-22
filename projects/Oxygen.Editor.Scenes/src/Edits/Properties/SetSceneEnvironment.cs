// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>Replaces the scene's whole environment: the revert of <see cref="SetSceneProperties"/> and of environment gestures.</summary>
/// <remarks>
/// One environment property can rewrite records no property names, as the backdrop choice rewrites the background and the
/// sky sphere, so only the whole environment restores exactly what an environment edit changed.
/// </remarks>
/// <param name="Environment">The environment.</param>
/// <param name="Label">The edit's name in the history.</param>
public sealed record SetSceneEnvironment(SceneEnvironmentData Environment, string Label) : ISceneEdit
{
    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return SceneEnvironmentRules.ProblemOf(this.Environment) is { } problem
            ? SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.PropertiesInconsistent, problem))
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = scope.Scene.Environment;
        var environment = SceneReferenceRetargeting.Retarget(this.Environment, scope.Moved) ?? this.Environment;
        if (environment == before)
        {
            return new SceneEditApplication.Applied(this, []);
        }

        scope.WarnIfDeleted(SceneAssetReferences.Environment(environment));
        scope.Scene.SetEnvironment(environment);
        return new SceneEditApplication.Applied(this with { Environment = before }, [new SceneChange.PropertiesChanged(Node: null, Differences(before, environment), EditPhase.Commit)]);
    }

    // The properties whose values differ, as the edit that this one reverts named them.
    private static PropertyId[] Differences(SceneEnvironmentData before, SceneEnvironmentData after)
    {
        var rules = SceneProperties.RulesOf(SceneProperties.SceneEnvironmentKind)!;
        var old = PropertyWrites.Read(rules, new SceneEnvironmentTarget(before), rules.Descriptors.Keys);
        var current = PropertyWrites.Read(rules, new SceneEnvironmentTarget(after), rules.Descriptors.Keys);
        return [.. old.Where(entry => !current.TryGetRaw(entry.Key, out var value) || !Equals(entry.Value, value)).Select(static entry => entry.Key)];
    }
}
