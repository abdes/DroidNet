// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Undoes <see cref="ApplyToSource"/>: the scene's file gets its previous bytes back and the instance its changes. It is
/// refused with <see cref="SceneEditCodes.SourceChanged"/> when the file changed since the changes were applied to it.
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Scene">The authored scene the changes were applied to.</param>
public sealed record RestoreSource(NodeId Instance, AssetPath Scene) : ISceneEdit
{
    /// <summary>Gets the instance's changes once the applied ones left it, which it holds now.</summary>
    public required InstanceChanges Changes { get; init; }

    /// <summary>Gets the instance's changes before they were applied, which it gets back.</summary>
    public required InstanceChanges Restored { get; init; }

    /// <summary>Gets the bytes the apply wrote to the scene's file, which it holds now; the edit keeps its own copy.</summary>
    public required ReadOnlyMemory<byte> Applied
    {
        get;
        init => field = value.ToArray();
    }

    /// <summary>Gets the bytes the scene's file held before the apply, which it gets back; the edit keeps its own copy.</summary>
    public required ReadOnlyMemory<byte> Previous
    {
        get;
        init => field = value.ToArray();
    }

    /// <inheritdoc/>
    public string Label => $"Apply to {SourceSceneWrite.NameOf(this.Scene)}";

    /// <inheritdoc/>
    /// <remarks>The instance root, while no change moves: the nodes whose changes move are checked with their ancestors.</remarks>
    public IReadOnlyCollection<NodeId> Targets => this.InstancedTargets.Count == 0 ? [this.Instance] : [];

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeReference> InstancedTargets => SourceSceneWrite.Moved(this.Instance, this.Restored, this.Changes);

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Restored is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Restoring a source names the changes the instance gets back.", this.Instance));
        }

        return SourceSceneWrite.Validate(
            scope,
            this.Instance,
            this.Scene,
            this.Changes,
            this.Applied,
            this.Previous,
            $"{this.Scene?.Name} changed since the changes were applied to it, so they cannot be taken back. Change it in its own scene.");
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (SourceSceneWrite.Write(scope, this.Instance, this.Scene, this.Restored, this.Previous, out var failure) is not { } changes)
        {
            return new SceneEditApplication.Rejected(SceneEditValidation.Refused(failure!));
        }

        return new SceneEditApplication.Applied(
            new ApplyToSource(this.Instance, this.Scene)
            {
                Changes = this.Restored,
                Remaining = this.Changes,
                Previous = this.Previous,
                Applied = this.Applied,
            },
            changes);
    }
}
