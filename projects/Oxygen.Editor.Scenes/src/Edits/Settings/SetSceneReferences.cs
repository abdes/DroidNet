// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Settings;

/// <summary>Replaces the scene's asset references: scripts, input actions, physics sidecars and extra assets.</summary>
public sealed record SetSceneReferences : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetSceneReferences"/> class.</summary>
    /// <param name="references">The complete reference set; the edit keeps its own copy.</param>
    public SetSceneReferences(SceneReferencesData references)
    {
        ArgumentNullException.ThrowIfNull(references);
        this.References = Copy(references);
    }

    /// <summary>Gets the references the scene takes.</summary>
    public SceneReferencesData References { get; }

    /// <inheritdoc/>
    public string Label => "Edit Scene References";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return Check(this.References) is { } error
            ? SceneEditValidation.Invalid(error)
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = Copy(scope.Scene.References);
        var references = Copy(SceneReferenceRetargeting.Retarget(this.References, scope.Moved) ?? this.References);
        if (AreEqual(before, references))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        scope.WarnIfDeleted(references.Scripts.Concat(references.InputActions).Concat(references.PhysicsSidecars));
        scope.Scene.SetReferences(references);
        return new SceneEditApplication.Applied(new SetSceneReferences(before), [new SceneChange.SceneReferencesChanged()]);
    }

    private static SceneDiagnostic? Check(SceneReferencesData references)
    {
        if (references.Scripts is null || references.InputActions is null || references.PhysicsSidecars is null || references.ExtraAssets is null)
        {
            return SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "Scene reference collections cannot be null.");
        }

        var typed = AssetReferenceRules.Check(references.Scripts, AssetFileTypes.Script)
            ?? AssetReferenceRules.Check(references.InputActions, AssetFileTypes.InputAction)
            ?? AssetReferenceRules.Check(references.PhysicsSidecars, AssetFileTypes.PhysicsScene);
        if (typed is not null)
        {
            return SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, typed);
        }

        return references.ExtraAssets.FirstOrDefault(static path => string.IsNullOrWhiteSpace(path) || !VirtualPath.IsCanonical(path)) is { } invalid
            ? SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, $"Extra asset path '{invalid}' must be an absolute canonical virtual path.")
            : null;
    }

    private static SceneReferencesData Copy(SceneReferencesData references)
        => new()
        {
            Scripts = [.. references.Scripts ?? []],
            InputActions = [.. references.InputActions ?? []],
            PhysicsSidecars = [.. references.PhysicsSidecars ?? []],
            ExtraAssets = [.. references.ExtraAssets ?? []],
        };

    private static bool AreEqual(SceneReferencesData left, SceneReferencesData right)
        => left.Scripts.SequenceEqual(right.Scripts)
            && left.InputActions.SequenceEqual(right.InputActions)
            && left.PhysicsSidecars.SequenceEqual(right.PhysicsSidecars)
            && left.ExtraAssets.SequenceEqual(right.ExtraAssets, StringComparer.Ordinal);
}
