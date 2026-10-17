// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.LivePreview.Content;

/// <summary>Starts the preview content demand of a workspace.</summary>
public static class ContentDemands
{
    /// <summary>Starts demanding preview cooks for the assets the workspace's open scene documents reference.</summary>
    /// <param name="documents">The workspace's open documents.</param>
    /// <param name="assets">Resolves each asset's identity and cook state.</param>
    /// <param name="pipeline">Cooks an asset for preview.</param>
    /// <param name="projects">The active project.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    /// <returns>The demand; disposing it cancels every demand and stops following the documents.</returns>
    public static IDisposable Start(
        OpenDocuments documents,
        IAssetFeed assets,
        IContentPipelineService pipeline,
        IProjectContextService projects,
        ILoggerFactory? loggerFactory = null)
        => new SceneContentDemand(documents, assets, pipeline, projects, loggerFactory);
}
