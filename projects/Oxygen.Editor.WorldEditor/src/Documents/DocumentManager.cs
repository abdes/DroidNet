// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Manages the lifecycle of documents in the World Editor, handling requests to open or create documents.
/// </summary>
public sealed partial class DocumentManager : IDisposable
{
    private readonly ILogger logger;
    private readonly IDocumentService documentService;
    private readonly IMessenger messenger;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectUsageService projectUsage;
    private readonly WindowId windowId;

    /// <summary>
    /// Initializes a new instance of the <see cref="DocumentManager"/> class.
    /// </summary>
    /// <param name="documentService">The service used to manage documents.</param>
    /// <param name="messenger">The messenger for inter-component communication.</param>
    /// <param name="windowId">The identifier of the window associated with this manager.</param>
    /// <param name="loggerFactory">Optional logger factory for logging.</param>
    public DocumentManager(
        IDocumentService documentService,
        IMessenger messenger,
        IProjectContextService projectContextService,
        IProjectUsageService projectUsage,
        WindowId windowId,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentManager>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentManager>();

        this.documentService = documentService;
        this.messenger = messenger;
        this.projectContextService = projectContextService;
        this.projectUsage = projectUsage;
        this.windowId = windowId;

        this.messenger.Register<OpenSceneRequestMessage>(this, this.OnOpenSceneRequested);
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.messenger.UnregisterAll(this);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Opens or activates the specified scene document for this workspace.
    /// </summary>
    /// <param name="scene">The scene to open.</param>
    /// <returns><see langword="true"/> when the scene document is open and selected.</returns>
    public async Task<bool> OpenSceneAsync(World.Scene scene)
    {
        this.LogOnOpenSceneRequested(scene);

        if (this.windowId.Value == 0)
        {
            this.LogCannotOpenSceneWindowIdInvalid(scene);
            return false;
        }

        // Check if the document is already open
        var openDocs = this.documentService.GetOpenDocuments(this.windowId);
        if (openDocs.Any(d => d.DocumentId == scene.Id))
        {
            this.LogReactivatingExistingScene(scene);

            var selected = await this.documentService.SelectDocumentAsync(this.windowId, scene.Id).ConfigureAwait(true);
            if (!selected)
            {
                this.LogSceneReactivationError(scene);
                return false;
            }

            this.LogReactivatedExistingScene(scene);
            await this.MarkSceneActivatedAsync(scene).ConfigureAwait(true);
            return true;
        }

        // Create metadata for the new scene document
        var metadataNew = new SceneDocumentMetadata(scene.Id) { Title = scene.Name };

        var openedId = await this.documentService.OpenDocumentAsync(this.windowId, metadataNew).ConfigureAwait(true);
        if (openedId == Guid.Empty)
        {
            this.LogSceneOpeningAborted(scene);
            return false;
        }

        this.LogOpenedNewScene(scene);
        await this.MarkSceneActivatedAsync(scene).ConfigureAwait(true);
        return true;
    }

    private async void OnOpenSceneRequested(object recipient, OpenSceneRequestMessage message)
    {
        var opened = await this.OpenSceneAsync(message.Scene).ConfigureAwait(true);
        message.Reply(opened);
    }

    private async Task MarkSceneActivatedAsync(World.Scene scene)
    {
        scene.Project.ActiveScene = scene;

        if (this.projectContextService.ActiveProject is not { } project)
        {
            return;
        }

        try
        {
            await this.projectUsage.UpdateLastOpenedSceneAsync(project.Name, project.ProjectRoot, scene.Name).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.logger.LogWarning(ex, "Failed to persist last opened scene {SceneName} for project {ProjectName}.", scene.Name, project.Name);
        }
    }
}
