// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Settings;

/// <summary>Replaces how the scene starts when played: its starting camera and its starting input contexts.</summary>
/// <remarks>Nothing reaches the engine: editor viewports keep their own cameras and input.</remarks>
public sealed record SetScenePlay : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="SetScenePlay"/> class.</summary>
    /// <param name="play">The complete play settings; the edit keeps its own copy.</param>
    public SetScenePlay(ScenePlayData play)
    {
        ArgumentNullException.ThrowIfNull(play);
        this.Play = play with { StartingInputContexts = [.. play.StartingInputContexts ?? []] };
    }

    /// <summary>Gets the play settings the scene takes.</summary>
    public ScenePlayData Play { get; }

    /// <inheritdoc/>
    public string Label => "Edit Play Settings";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets => [];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        // The settings as given are checked first: following moved assets needs well-formed entries.
        return (Check(scope.Scene, this.Play) ?? Check(scope.Scene, this.Resolved(scope))) is { } error
            ? SceneEditValidation.Invalid(error)
            : SceneEditValidation.Valid;
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var before = scope.Scene.Play;
        var play = this.Resolved(scope);
        if (ScenePlayData.AreEquivalent(before, play))
        {
            return new SceneEditApplication.Applied(this, []);
        }

        scope.WarnIfDeleted(play.StartingInputContexts.Select(static entry => (Uri?)entry.Context));
        scope.Scene.SetPlay(play);
        return new SceneEditApplication.Applied(new SetScenePlay(before), [new SceneChange.ScenePlayChanged()]);
    }

    private static SceneDiagnostic? Check(Scene scene, ScenePlayData play)
    {
        if (play.StartingInputContexts.Any(static entry => entry is null))
        {
            return SceneDiagnostic.Error(SceneEditCodes.EntryMissing, "The starting input context list is missing an entry.");
        }

        if (play.StartingCamera is { } camera
            && !scene.AllNodes.Any(node => node.Id == camera && node.Components.OfType<CameraComponent>().Any()))
        {
            return SceneDiagnostic.Error(SceneEditCodes.PlayCameraInvalid, "The starting camera must be a camera node of this scene.");
        }

        if (AssetReferenceRules.Check(play.StartingInputContexts.Select(static entry => (Uri?)entry.Context), AssetFileTypes.InputMappingContext) is { } contexts)
        {
            return SceneDiagnostic.Error(SceneEditCodes.AssetReferenceInvalid, contexts);
        }

        return play.StartingInputContexts.Select(static entry => entry.Context).Distinct().Count() != play.StartingInputContexts.Count
            ? SceneDiagnostic.Error(SceneEditCodes.EntryDuplicate, "An input context can start only once.")
            : null;
    }

    private ScenePlayData Resolved(SceneEditScope scope) => SceneReferenceRetargeting.Retarget(this.Play, scope.Moved) ?? this.Play;
}
