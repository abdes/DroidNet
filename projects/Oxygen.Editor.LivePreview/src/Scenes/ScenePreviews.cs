// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Creates the scene preview of a workspace.</summary>
public static class ScenePreviews
{
    /// <summary>Creates the preview that follows a workspace's open scene documents.</summary>
    /// <remarks>The preview is disposable; the workspace container that holds it disposes it.</remarks>
    /// <param name="engine">The embedded engine.</param>
    /// <param name="documents">The workspace's open documents.</param>
    /// <param name="session">The project session whose scene documents the preview shows.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    /// <param name="operationResults">Publishes the engine's rejections and failures.</param>
    /// <param name="assetFeed">Reports which assets are mounted, so geometry waits for its asset.</param>
    /// <returns>The scene preview.</returns>
    public static IScenePreview Create(
        IEngineService engine,
        OpenDocuments documents,
        IProjectSession session,
        ILoggerFactory? loggerFactory = null,
        IOperationResultPublisher? operationResults = null,
        IAssetFeed? assetFeed = null)
        => new ScenePreview(engine, documents, session, loggerFactory, operationResults, assetFeed);
}
