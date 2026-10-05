// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Documents;

public sealed partial class DocumentManager
{
    /// <summary>Reloads the previous saved scene after a replacement failed following retirement.</summary>
    /// <returns>Whether the previous saved scene was successfully staged and installed.</returns>
    public Task<bool> ReloadPreviousSceneAsync()
    {
        if (this.previousSavedScene is not { } previous || this.projectManager.CurrentProject is not { } project
            || project.ProjectInfo.Id != previous.projectId
            || !string.Equals(project.ProjectInfo.Location, previous.projectRoot, StringComparison.OrdinalIgnoreCase)
            || project.Scenes.FirstOrDefault(scene => scene.Id == previous.sceneId) is not { } scene)
        {
            return Task.FromResult(false);
        }

        return this.OpenSceneAsync(scene);
    }

    private void PublishSceneLoadFailure(Scene scene, bool unavailable)
    {
        if (unavailable)
        {
            _ = this.messenger.Send(new SceneUnavailableMessage(this.windowId, this.previousSavedScene is not null));
        }

        if (this.operationResults is not { } publisher || this.statusReducer is not { } reducer)
        {
            return;
        }

        _ = SceneOperationResults.PublishFailure(
            publisher,
            reducer,
            "Scene.Load",
            FailureDomain.Document,
            DiagnosticCodes.DocumentPrefix + (unavailable ? "SCENE_UNAVAILABLE" : "SCENE_LOAD_FAILED"),
            unavailable ? "Scene unavailable" : "Scene was not loaded",
            unavailable
                ? "The previous editable scene was retired, but the replacement could not be installed. Reload the previous saved scene or open another scene."
                : "The source could not be validated. The current scene, selection and clipboard were kept.",
            new AffectedScope { DocumentId = scene.Id, DocumentName = scene.Name });
    }
}
