// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.TimeMachine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Owns edits of how the scene starts when played. Each edit replaces the whole play settings, so one undo restores
/// the starting camera and the starting input contexts exactly. Nothing is synchronized to the engine: editor panes
/// keep their own cameras and input.
/// </summary>
public sealed partial class SceneDocumentCommandService
{
    private const string ScenePlayInvalidCode = "SCENE_PLAY_INVALID";

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditScenePlayAsync(SceneDocumentCommandContext context, ScenePlayData play)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new(Succeeded: false);
        }

        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(play);
        if (ValidatePlay(context.Scene, play) is { } error)
        {
            return this.ValidationFailure(SceneOperationKinds.EditScenePlay, ScenePlayInvalidCode, "Play settings were not changed", error, context);
        }

        var before = context.Scene.Play;
        if (ScenePlayData.AreEquivalent(before, play))
        {
            return SceneCommandResult.Success;
        }

        context.Scene.SetPlay(play);
        var after = context.Scene.Play;
        context.History.AddChange("Restore Play Settings", async () =>
            await this.ApplyScenePlayForHistoryAsync(context, before, after).ConfigureAwait(true));
        _ = this.AdvanceRevision(context);
        return SceneCommandResult.Success;
    }

    private static string? ValidatePlay(World.Scene scene, ScenePlayData play)
    {
        if (play.StartingInputContexts is null || play.StartingInputContexts.Any(static entry => entry is null))
        {
            return "The starting input context list is missing an entry.";
        }

        if (play.StartingCamera is { } camera
            && !scene.AllNodes.Any(node => node.Id == camera && node.Components.OfType<CameraComponent>().Any()))
        {
            return "The starting camera must be a camera node of this scene.";
        }

        if (ValidateTypedReferences(play.StartingInputContexts.Select(static entry => entry.Context), AssetFileTypes.InputMappingContext) is { } contexts)
        {
            return contexts;
        }

        return play.StartingInputContexts.Select(static entry => entry.Context).Distinct().Count() != play.StartingInputContexts.Count
            ? "An input context can start only once."
            : null;
    }

    private async Task ApplyScenePlayForHistoryAsync(
        SceneDocumentCommandContext context,
        ScenePlayData play,
        ScenePlayData inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        var resolved = SceneReferenceRetargeting.Retarget(play, this.RedirectOrNull) ?? play;
        this.WarnIfDeleted(context, resolved.StartingInputContexts.Select(static entry => (Uri?)entry.Context));
        context.Scene.SetPlay(resolved);
        context.History.AddChange("Reapply Play Settings", async () =>
            await this.ApplyScenePlayForHistoryAsync(context, inverse, play).ConfigureAwait(true));
        _ = this.AdvanceRevision(context);
    }
}
