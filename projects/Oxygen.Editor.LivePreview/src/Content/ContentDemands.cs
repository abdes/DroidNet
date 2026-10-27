// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;

namespace Oxygen.Editor.LivePreview.Content;

/// <summary>Starts the preview content demand of a workspace.</summary>
public static class ContentDemands
{
    /// <summary>Starts demanding preview cooks for the assets the scenes of the workspace's open documents reference.</summary>
    /// <param name="documents">The workspace's open documents.</param>
    /// <param name="scenes">The scene each open document shows, a model document's preview scene included.</param>
    /// <param name="assets">Resolves each asset's identity and cook state.</param>
    /// <param name="pipeline">Cooks an asset for preview.</param>
    /// <param name="session">The project session, whose current content mounts a demand cooks in.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    /// <returns>The demand; disposing it cancels every demand and stops following the documents.</returns>
    public static IDisposable Start(
        OpenDocuments documents,
        DocumentScenes scenes,
        IAssetFeed assets,
        IContentPipelineService pipeline,
        IProjectSession session,
        ILoggerFactory? loggerFactory = null)
        => new SceneContentDemand(documents, scenes, assets, pipeline, session, loggerFactory);
}
