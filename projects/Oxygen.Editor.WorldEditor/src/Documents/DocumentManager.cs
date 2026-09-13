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
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspection;

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
    private readonly IMaterialDocumentService materialDocumentService;
    private readonly WindowId windowId;

    /// <summary>
    /// Initializes a new instance of the <see cref="DocumentManager"/> class.
    /// </summary>
    /// <param name="documentService">The service used to manage documents.</param>
    /// <param name="messenger">The messenger for inter-component communication.</param>
    /// <param name="projectContextService">The active project and its activation lifetime.</param>
    /// <param name="projectUsage">The workspace's recent-scene persistence.</param>
    /// <param name="materialDocumentService">The material authoring service.</param>
    /// <param name="windowId">The identifier of the window associated with this manager.</param>
    /// <param name="loggerFactory">Optional logger factory for logging.</param>
    public DocumentManager(
        IDocumentService documentService,
        IMessenger messenger,
        IProjectContextService projectContextService,
        IProjectUsageService projectUsage,
        IMaterialDocumentService materialDocumentService,
        WindowId windowId,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentManager>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentManager>();

        this.documentService = documentService;
        this.messenger = messenger;
        this.projectContextService = projectContextService;
        this.projectUsage = projectUsage;
        this.materialDocumentService = materialDocumentService;
        this.windowId = windowId;

        this.messenger.Register<OpenSceneRequestMessage>(this, this.OnOpenSceneRequested);
        this.messenger.Register<OpenMaterialRequestMessage>(this, this.OnOpenMaterialRequested);
        this.messenger.Register<CreateMaterialRequestMessage>(this, this.OnCreateMaterialRequested);
        this.messenger.Register<OpenCookedInspectionRequestMessage>(this, (_, message) => message.Reply(this.OpenInspectionAsync(message)));
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

    /// <summary>
    /// Opens or activates the specified material document for this workspace.
    /// </summary>
    /// <param name="materialUri">The material source asset URI.</param>
    /// <param name="title">The document title.</param>
    /// <returns><see langword="true"/> when the material document is open and selected.</returns>
    public async Task<bool> OpenMaterialAsync(Uri materialUri, string title)
    {
        ArgumentNullException.ThrowIfNull(materialUri);

        if (this.windowId.Value == 0)
        {
            return false;
        }

        var openDocs = this.documentService.GetOpenDocuments(this.windowId);
        var existing = openDocs
            .OfType<MaterialDocumentMetadata>()
            .FirstOrDefault(d => UriValuesEqual(d.MaterialUri, materialUri));
        if (existing is not null)
        {
            return await this.documentService.SelectDocumentAsync(this.windowId, existing.DocumentId).ConfigureAwait(true);
        }

        var metadata = new MaterialDocumentMetadata(materialUri)
        {
            Title = string.IsNullOrWhiteSpace(title) ? Path.GetFileNameWithoutExtension(materialUri.AbsolutePath) : title,
        };

        var openedId = await this.documentService.OpenDocumentAsync(this.windowId, metadata).ConfigureAwait(true);
        return openedId != Guid.Empty;
    }

    private static bool UriValuesEqual(Uri left, Uri right)
        => string.Equals(left.ToString(), right.ToString(), StringComparison.OrdinalIgnoreCase);

    private async void OnOpenSceneRequested(object recipient, OpenSceneRequestMessage message)
    {
        var opened = await this.OpenSceneAsync(message.Scene).ConfigureAwait(true);
        message.Reply(opened);
    }

    private async Task<bool> OpenInspectionAsync(OpenCookedInspectionRequestMessage request)
    {
        if (this.windowId.Value == 0 || !ReferenceEquals(request.Project, this.projectContextService.ActiveProject))
        {
            return false;
        }

        var existing = this.documentService.GetOpenDocuments(this.windowId).OfType<CookedInspectionDocumentMetadata>()
            .FirstOrDefault(document => ReferenceEquals(document.Project, request.Project) && document.ScopeUri == request.ScopeUri);
        if (existing is not null)
        {
            existing.RequestRefresh(request.Validate);
            return await this.documentService.SelectDocumentAsync(this.windowId, existing.DocumentId).ConfigureAwait(true);
        }

        var name = request.ScopeUri is null ? request.Project.Name : Path.GetFileName(Uri.UnescapeDataString(request.ScopeUri.AbsolutePath).TrimEnd('/'));
        var metadata = new CookedInspectionDocumentMetadata(request.Project, request.ScopeUri, request.Validate)
        {
            Title = "Inspect · " + (string.IsNullOrEmpty(name) || string.Equals(name, "Cooked", StringComparison.OrdinalIgnoreCase) ? request.Project.Name : name),
        };
        return await this.documentService.OpenDocumentAsync(this.windowId, metadata).ConfigureAwait(true) != Guid.Empty;
    }

    private async void OnOpenMaterialRequested(object recipient, OpenMaterialRequestMessage message)
    {
        var opened = await this.OpenMaterialAsync(message.MaterialUri, message.Title).ConfigureAwait(true);
        message.Reply(opened);
    }

    private async void OnCreateMaterialRequested(object recipient, CreateMaterialRequestMessage message)
    {
        try
        {
            var created = await this.materialDocumentService.CreateAsync(message.MaterialUri).ConfigureAwait(true);
            await this.materialDocumentService.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(true);
            _ = this.messenger.Send(new AssetsChangedMessage(message.MaterialUri));
            var opened = await this.OpenMaterialAsync(message.MaterialUri, message.Title).ConfigureAwait(true);
            message.Reply(opened);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogMaterialCreationFailed(ex, message.MaterialUri);
            message.Reply(response: false);
        }
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
            this.LogSceneUsageUpdateFailed(ex, scene.Name, project.Name);
        }
    }
}
