// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Has a model's import record written back as an edit found it and the model reimported, with the instance's changes as
/// they were: the revert of repairing a kept change in its model, and of that revert (scene instances 6.4).
/// </summary>
/// <param name="Instance">The instance root.</param>
/// <param name="Model">The model's source.</param>
/// <param name="Label">The history label of the edit it reverts.</param>
/// <remarks>
/// Refused with <see cref="SceneEditCodes.SourceChanged"/> when the record no longer holds the bytes the edit wrote, and
/// with <see cref="SceneEditCodes.InstanceUnavailable"/> when the instance's changes changed since.
/// </remarks>
public sealed record RestoreModelRecord(NodeId Instance, AssetPath Model, string Label) : ISceneEdit
{
    /// <summary>Gets the instance's changes now.</summary>
    public required InstanceChanges Changes { get; init; }

    /// <summary>Gets the instance's changes it gets back.</summary>
    public required InstanceChanges Restored { get; init; }

    /// <summary>Gets the bytes the record holds now; the edit keeps its own copy.</summary>
    public required ReadOnlyMemory<byte> Applied
    {
        get;
        init => field = value.ToArray();
    }

    /// <summary>Gets the bytes the record gets back; the edit keeps its own copy.</summary>
    public required ReadOnlyMemory<byte> Previous
    {
        get;
        init => field = value.ToArray();
    }

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [this.Instance];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // A deserialized edit can still carry nulls.
        if (this.Model is null || this.Changes is null || this.Restored is null || this.Applied.IsEmpty || this.Previous.IsEmpty)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "Restoring a model's import settings names the model, the instance's changes and the record's bytes.", this.Instance));
        }

        if (KeptChangeEdits.InstanceOf(scope, this.Instance) is not { } instance)
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it has no kept changes.", this.Instance));
        }

        if (!this.Changes.MapAssets(asset => scope.Resolve(asset)!).Equals(instance.Changes))
        {
            return SceneEditValidation.Invalid(SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, "The instance's changes changed since, so the repair cannot be taken back.", this.Instance));
        }

        var model = scope.Resolve(this.Model)!;
        if (scope.ModelRecords is not { } records)
        {
            return SceneEditValidation.Refused(SceneDiagnostic.Error(SceneEditCodes.InstanceUnavailable, $"The import settings of {model.Name} cannot be written: no project is open.", this.Instance));
        }

        return records.Read(model) is { } current && current.AsSpan().SequenceEqual(this.Applied.Span)
            ? SceneEditValidation.Valid
            : SceneEditValidation.Refused(SceneDiagnostic.Error(
                SceneEditCodes.SourceChanged,
                $"The import settings of {model.Name} changed since the kept change was reassigned, so it cannot be taken back. Change them in the Model Editor.",
                this.Instance));
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var instance = KeptChangeEdits.InstanceOf(scope, this.Instance)!;
        var model = scope.Resolve(this.Model)!;
        scope.ModelRecords!.Replace(model, this.Applied, this.Previous);
        var before = instance.Changes;
        instance.Changes = this.Restored.MapAssets(asset => scope.Resolve(asset)!);
        return new SceneEditApplication.Applied(
            this with { Changes = instance.Changes, Restored = before, Applied = this.Previous, Previous = this.Applied },
            [KeptChangeEdits.Changed(this.Instance, before, instance.Changes)]);
    }
}
