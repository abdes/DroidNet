// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Manages the lifecycle of documents in the World Editor, handling requests to open or create documents.
/// </summary>
public sealed partial class DocumentManager : IDisposable
{
    private readonly ILogger logger;
    private readonly IDocumentService documentService;
    private readonly IMessenger messenger;
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
        WindowId windowId,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentManager>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentManager>();

        this.documentService = documentService;
        this.messenger = messenger;
        this.windowId = windowId;

        this.messenger.Register<OpenSceneRequestMessage>(this, this.OnOpenSceneRequested);
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.messenger.UnregisterAll(this);
        GC.SuppressFinalize(this);
    }

    private async void OnOpenSceneRequested(object recipient, OpenSceneRequestMessage message)
    {
        this.LogOnOpenSceneRequested(message.Scene);

        if (this.windowId.Value == 0)
        {
            this.LogCannotOpenSceneWindowIdInvalid(message.Scene);
            message.Reply(response: false);
            return;
        }

        // Check if the document is already open
        var openDocs = this.documentService.GetOpenDocuments(this.windowId);
        if (openDocs.Any(d => d.DocumentId == message.Scene.Id))
        {
            this.LogReactivatingExistingScene(message.Scene);

            var selected = await this.documentService.SelectDocumentAsync(this.windowId, message.Scene.Id).ConfigureAwait(true);
            if (!selected)
            {
                this.LogSceneReactivationError(message.Scene);
                message.Reply(response: false);
                return;
            }

            this.LogReactivatedExistingScene(message.Scene);
            message.Reply(response: true);
            return;
        }

        // Create metadata for the new scene document
        var metadataNew = new SceneDocumentMetadata(message.Scene.Id)
        {
            Title = message.Scene.Name,
            IsClosable = false,
        };

        var openedId = await this.documentService.OpenDocumentAsync(this.windowId, metadataNew).ConfigureAwait(true);
        if (openedId == Guid.Empty)
        {
            this.LogSceneOpeningAborted(message.Scene);
            message.Reply(response: false);
            return;
        }

        this.LogOpenedNewScene(message.Scene);

        message.Reply(response: true);
    }
}
