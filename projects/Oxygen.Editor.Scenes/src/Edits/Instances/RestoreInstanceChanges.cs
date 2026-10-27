// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Gives an instance back the exact changes it had, as undoing the removal or reassignment of kept changes does (scene
/// instances 8.3).
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Label">The history label of the edit it reverts.</param>
public sealed record RestoreInstanceChanges(NodeId Instance, string Label) : ISceneEdit
{
    /// <summary>Gets the changes the instance holds now.</summary>
    public required InstanceChanges Changes { get; init; }

    /// <summary>Gets the changes the instance gets back.</summary>
    public required InstanceChanges Restored { get; init; }

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.Changes is null || this.Restored is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Restoring an instance's changes names the changes it holds and those it gets back.", this.Instance));
        }

        if (KeptChangeEdits.InstanceOf(scope, this.Instance) is not { } instance)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no changes.", this.Instance));
        }

        return this.Changes.MapAssets(asset => scope.Resolve(asset)!).Equals(instance.Changes)
            ? SceneEditValidation.Valid
            : SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, "The instance's changes changed since, so they cannot be restored.", this.Instance));
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var instance = KeptChangeEdits.InstanceOf(scope, this.Instance)!;
        var before = instance.Changes;
        instance.Changes = this.Restored.MapAssets(asset => scope.Resolve(asset)!);
        return new SceneEditApplication.Applied(
            this with { Changes = instance.Changes, Restored = before },
            [KeptChangeEdits.Changed(this.Instance, before, instance.Changes)]);
    }
}
