// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Removes kept changes of an instance, the changes of nodes its source no longer holds, so they no longer wait for
/// their node (scene instances 6.4, 8.3).
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Paths">The node paths whose changes are removed, each a kept change.</param>
/// <remarks>
/// Kept changes are told apart against the source's complete structure: while it is not read, the edit is refused with
/// <see cref="SceneEditCodes.InstanceUnavailable"/>, as it is for a path whose node the source holds.
/// </remarks>
public sealed record RemoveKeptChanges(NodeId Instance, IReadOnlyList<NodePath> Paths) : ISceneEdit
{
    /// <summary>The history label.</summary>
    public const string HistoryLabel = "Remove Kept Changes";

    /// <inheritdoc/>
    public string Label => HistoryLabel;

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.Paths is not { Count: > 0 })
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Name the kept changes to remove.", this.Instance));
        }

        if (KeptChangeEdits.InstanceOf(scope, this.Instance) is not { } instance)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no kept changes.", this.Instance));
        }

        if (KeptChangeEdits.StructureOf(scope, this.Instance, instance, out var unavailable) is not { } structure)
        {
            return SceneEditValidation.Refused(unavailable!);
        }

        var refused = this.Paths.Select(path => KeptChangeEdits.RefuseNotKept(this.Instance, instance, structure, path)).FirstOrDefault(static refusal => refusal is not null);
        return refused is null ? SceneEditValidation.Valid : SceneEditValidation.Refused(refused);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var instance = KeptChangeEdits.InstanceOf(scope, this.Instance)!;
        var before = instance.Changes;
        instance.Changes = this.Paths.Aggregate(before, static (changes, path) => changes.With(path, InstancedNodeChanges.None));
        return new SceneEditApplication.Applied(
            new RestoreInstanceChanges(this.Instance, HistoryLabel) { Changes = instance.Changes, Restored = before },
            [KeptChangeEdits.Changed(this.Instance, before, instance.Changes)]);
    }
}
