// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Moves changes of an instance into an authored scene its node paths pass through (scene instances 6.7, 8.3): the
/// scene's file gets them, as stored values of the nodes it owns and as changes of the instance roots it holds, and they
/// leave the instance. Every instance of that scene picks them up once it is cooked.
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Scene">The authored scene the changes go to: the instance's source or a scene below it.</param>
/// <remarks>
/// <see cref="SourceApplication.BuildAsync"/> builds the edit, with the file's bytes before and after. The source scene
/// is not open, so the edit writes its file and keeps its previous bytes: <see cref="RestoreSource"/>, the undo, writes
/// them back. Both are refused with <see cref="SceneEditCodes.SourceChanged"/> when the file no longer holds the bytes
/// they expect, and with <see cref="SceneEditCodes.InstanceUnavailable"/> when the instance's changes are not those the
/// edit was built from. A model is not an authored scene, so it is refused with
/// <see cref="SceneEditCodes.AssetReferenceInvalid"/>. Changes of nodes the scene does not hold stay on the instance and
/// are listed in a <see cref="SceneEditCodes.NodePathUnresolved"/> warning.
/// </remarks>
public sealed record ApplyToSource(NodeId Instance, AssetPath Scene) : ISceneEdit
{
    /// <summary>Gets the instance's changes the edit was built from.</summary>
    public required InstanceChanges Changes { get; init; }

    /// <summary>Gets the instance's changes once the applied ones left it.</summary>
    public required InstanceChanges Remaining { get; init; }

    /// <summary>Gets the bytes the scene's file held when the edit was built; the edit keeps its own copy.</summary>
    public required ReadOnlyMemory<byte> Previous
    {
        get;
        init => field = value.ToArray();
    }

    /// <summary>Gets the bytes the edit writes to the scene's file; the edit keeps its own copy.</summary>
    public required ReadOnlyMemory<byte> Applied
    {
        get;
        init => field = value.ToArray();
    }

#pragma warning disable SA1500 // Braces for multi-line statements should not share line
#pragma warning disable SA1513 // Closing brace should be followed by blank line
    /// <summary>Gets the names of the selected changes that stay because the scene does not hold their nodes.</summary>
    public IReadOnlyList<string> Kept
    {
        get;
        init => field = value is null ? null! : [.. value];
    } = [];
#pragma warning restore SA1513 // Closing brace should be followed by blank line
#pragma warning restore SA1500 // Braces for multi-line statements should not share line

    /// <inheritdoc/>
    public string Label => $"Apply to {SourceSceneWrite.NameOf(this.Scene)}";

    /// <inheritdoc/>
    /// <remarks>The instance root, while no change moves: the nodes whose changes move are checked with their ancestors.</remarks>
    public IReadOnlyCollection<NodeId> Targets => this.InstancedTargets.Count == 0 ? [this.Instance] : [];

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeReference> InstancedTargets => SourceSceneWrite.Moved(this.Instance, this.Changes, this.Remaining);

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (this.Remaining is null || this.Kept is null)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Applying to a source names the changes that remain on the instance.", this.Instance));
        }

        return SourceSceneWrite.Validate(
            scope,
            this.Instance,
            this.Scene,
            this.Changes,
            this.Previous,
            this.Applied,
            $"{this.Scene?.Name} changed since the changes were read. Apply them again.");
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (SourceSceneWrite.Write(scope, this.Instance, this.Scene, this.Remaining, this.Applied, out var failure) is not { } changes)
        {
            return new SceneEditApplication.Rejected(SceneEditValidation.Refused(failure!));
        }

        if (this.Kept.Count != 0)
        {
            scope.Warn([SceneDiagnostic.Warning(
                SceneEditCodes.NodePathUnresolved,
                $"{this.Scene.Name} does not hold the nodes of {this.Kept.Count} change(s), which stay on the instance: {string.Join(", ", this.Kept)}.",
                this.Instance)]);
        }

        return new SceneEditApplication.Applied(
            new RestoreSource(this.Instance, this.Scene)
            {
                Changes = this.Remaining,
                Restored = this.Changes,
                Applied = this.Applied,
                Previous = this.Previous,
            },
            changes);
    }
}
