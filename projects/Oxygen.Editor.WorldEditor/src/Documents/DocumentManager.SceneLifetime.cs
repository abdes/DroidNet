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
        => this.previousSavedScene is { } previous && this.projectContextService.ActiveProject is { } project
            && project.ProjectId == previous.projectId
            && string.Equals(project.ProjectRoot, previous.projectRoot, StringComparison.OrdinalIgnoreCase)
                ? this.OpenSceneAsync(previous.sceneUri)
                : Task.FromResult(false);

    private void PublishSceneLoadFailure(Uri sceneUri, bool unavailable, string? problem = null)
    {
        if (unavailable)
        {
            _ = this.messenger.Send(new SceneUnavailableMessage(this.windowId, this.previousSavedScene is not null));
        }

        if (this.operationResults is not { } publisher || this.statusReducer is not { } reducer)
        {
            return;
        }

        var title = unavailable ? "Scene unavailable" : "Scene was not loaded";
        var message = unavailable
                ? "The previous editable scene was retired, but the replacement could not be installed. Reload the previous saved scene or open another scene."
                : (problem ?? "The source could not be validated.") + " The current scene, selection and clipboard were kept.";

        _ = SceneOperationResults.PublishFailure(
            publisher,
            reducer,
            "Scene.Load",
            FailureDomain.Document,
            DiagnosticCodes.DocumentPrefix + (unavailable ? "SCENE_UNAVAILABLE" : "SCENE_LOAD_FAILED"),
            title,
            message,
            new AffectedScope { DocumentName = SceneContent.NameOf(Uri.UnescapeDataString(sceneUri.AbsolutePath)), AssetVirtualPath = Uri.UnescapeDataString(sceneUri.AbsolutePath) });
    }
}
