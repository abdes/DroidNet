// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;

namespace Oxygen.Editor.Documents;

/// <summary>
/// Document service for WorldEditor, integrates with Aura document tabs.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class EditorDocumentService(ILoggerFactory? loggerFactory = null)
    : IDocumentService, IDocumentServiceState
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<EditorDocumentService>() ?? NullLoggerFactory.Instance.CreateLogger<EditorDocumentService>();

    // Store documents per window
    private readonly Dictionary<WindowId, Dictionary<Guid, IDocumentMetadata>> windowDocs = [];
    private readonly Dictionary<WindowId, Guid> activeDocuments = [];

    /// <inheritdoc/>
    public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

    /// <inheritdoc/>
    public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

    /// <inheritdoc/>
    public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

    /// <inheritdoc/>
    public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

    /// <inheritdoc/>
    public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

    /// <inheritdoc/>
    public event EventHandler<DocumentMetadataChangedEventArgs>? DocumentMetadataChanged;

    /// <inheritdoc/>
    public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

    /// <inheritdoc/>
    public async Task<Guid> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        var documentId = metadata.DocumentId;
        this.LogOpenDocumentCalled(windowId.Value, documentId);

        var docs = this.GetOrCreateWindowDocuments(windowId);

        // Ensure only one Scene document is open per window. If the incoming metadata
        // is a SceneDocumentMetadata, attempt to close any existing scene document
        // in this window before opening the new one. If the close is vetoed, abort.
        if (metadata is SceneDocumentMetadata)
        {
            Guid? existingSceneId = null;
            foreach (var kv in docs)
            {
                if (kv.Value is SceneDocumentMetadata)
                {
                    existingSceneId = kv.Key;
                    break;
                }
            }

            if (existingSceneId.HasValue)
            {
                var closed = await this.CloseDocumentAsync(windowId, existingSceneId.Value, force: false).ConfigureAwait(false);
                if (!closed)
                {
                    this.LogDocumentOpenAborted(windowId.Value, documentId);
                    return Guid.Empty;
                }

                // Closing the last document removes the dictionary entry; reacquire it so the
                // upcoming assignment is tracked in the global windowDocs map.
                docs = this.GetOrCreateWindowDocuments(windowId);
            }
        }

        docs[documentId] = metadata;
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, metadata, indexHint, shouldSelect));
        if (shouldSelect)
        {
            this.activeDocuments[windowId] = documentId;

            // Raise DocumentActivated for consistency with SelectDocumentAsync
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
        }

        return documentId;
    }

    /// <inheritdoc/>
    public async Task<bool> CloseDocumentAsync(WindowId windowId, Guid documentId, bool force = false)
    {
        this.LogCloseDocumentCalled(windowId.Value, documentId, force);
        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.TryGetValue(documentId, out var metadata))
        {
            this.LogCloseDocumentNotFound(windowId.Value, documentId);
            return false;
        }

        var closingArgs = new DocumentClosingEventArgs(windowId, metadata, force);
        this.DocumentClosing?.Invoke(this, closingArgs);
        if (!force)
        {
            var allowed = await closingArgs.WaitForVetoResultAsync().ConfigureAwait(false);
            if (!allowed)
            {
                return false;
            }
        }

        _ = docs.Remove(documentId);
        if (docs.Count == 0)
        {
            _ = this.windowDocs.Remove(windowId);
        }

        if (this.activeDocuments.TryGetValue(windowId, out var activeId) && activeId == documentId)
        {
            _ = this.activeDocuments.Remove(windowId);
        }

        this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(windowId, metadata));
        this.LogDocumentClosed(windowId.Value, documentId);
        return true;
    }

    /// <inheritdoc/>
    public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, Guid documentId)
    {
        this.LogDetachDocumentCalled(windowId.Value, documentId);

        if (this.windowDocs.TryGetValue(windowId, out var docs) && docs.TryGetValue(documentId, out var metadata))
        {
            _ = docs.Remove(documentId);
            if (docs.Count == 0)
            {
                _ = this.windowDocs.Remove(windowId);
            }

            if (this.activeDocuments.TryGetValue(windowId, out var activeId) && activeId == documentId)
            {
                _ = this.activeDocuments.Remove(windowId);
            }

            this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
            this.LogDetached(windowId.Value, documentId);
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        this.LogDetachDocumentNotFound(windowId.Value, documentId);
        return Task.FromResult<IDocumentMetadata?>(null);
    }

    /// <inheritdoc/>
    public async Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        this.LogAttachDocumentCalled(targetWindowId.Value, metadata.DocumentId);

        var docs = this.GetOrCreateWindowDocuments(targetWindowId);

        // If attaching a Scene document, replace any existing Scene document in the target window.
        if (metadata is SceneDocumentMetadata)
        {
            Guid? existingSceneId = null;
            foreach (var kv in docs)
            {
                if (kv.Value is SceneDocumentMetadata)
                {
                    existingSceneId = kv.Key;
                    break;
                }
            }

            if (existingSceneId.HasValue)
            {
                var closed = await this.CloseDocumentAsync(targetWindowId, existingSceneId.Value, force: false).ConfigureAwait(false);
                if (!closed)
                {
                    this.LogAttachDocumentAborted(targetWindowId.Value, metadata.DocumentId);
                    return false;
                }

                // Closing may remove the window entry; reacquire to keep the attach tracked.
                docs = this.GetOrCreateWindowDocuments(targetWindowId);
            }
        }

        docs[metadata.DocumentId] = metadata;
        if (shouldSelect)
        {
            this.activeDocuments[targetWindowId] = metadata.DocumentId;

            // Raise DocumentActivated for consistency with SelectDocumentAsync
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(targetWindowId, metadata.DocumentId));
        }

        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        this.LogAttached(targetWindowId.Value, metadata.DocumentId);
        return true;
    }

    /// <inheritdoc/>
    public Task<bool> UpdateMetadataAsync(WindowId windowId, Guid documentId, IDocumentMetadata metadata)
    {
        this.LogUpdateMetadataCalled(windowId.Value, documentId);

        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.ContainsKey(documentId))
        {
            this.LogUpdateMetadataFailed(windowId.Value, documentId);
            return Task.FromResult(false);
        }

        docs[documentId] = metadata;
        this.DocumentMetadataChanged?.Invoke(this, new DocumentMetadataChangedEventArgs(windowId, metadata));
        this.LogUpdateMetadataSuccess(windowId.Value, documentId);
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> SelectDocumentAsync(WindowId windowId, Guid documentId)
    {
        this.LogSelectDocumentCalled(windowId.Value, documentId);

        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.TryGetValue(documentId, out var metadata))
        {
            this.LogSelectDocumentNotFound(windowId.Value, documentId);
            return Task.FromResult(false);
        }

        this.activeDocuments[windowId] = documentId;
        this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
        this.LogDocumentActivatedInvoked(windowId.Value, documentId);
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public IReadOnlyList<IDocumentMetadata> GetOpenDocuments(WindowId windowId)
        => !this.windowDocs.TryGetValue(windowId, out var docs) || docs.Count == 0
            ? Array.Empty<IDocumentMetadata>()
            : docs.Values.ToList();

    /// <inheritdoc/>
    public Guid? GetActiveDocumentId(WindowId windowId)
        => this.activeDocuments.TryGetValue(windowId, out var documentId) && documentId != Guid.Empty
            ? documentId
            : null;

    private Dictionary<Guid, IDocumentMetadata> GetOrCreateWindowDocuments(WindowId windowId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs))
        {
            docs = [];
            this.windowDocs[windowId] = docs;
        }

        return docs;
    }
}
