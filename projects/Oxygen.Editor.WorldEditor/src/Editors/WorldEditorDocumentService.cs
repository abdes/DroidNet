// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
/// Document service for WorldEditor, integrates with Aura document tabs.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class WorldEditorDocumentService(ILoggerFactory? loggerFactory = null) : IDocumentService
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<WorldEditorDocumentService>() ?? NullLoggerFactory.Instance.CreateLogger<WorldEditorDocumentService>();

    // Store documents per window
    private readonly Dictionary<WindowId, Dictionary<Guid, IDocumentMetadata>> windowDocs = [];

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
    public Task<Guid> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        this.LogOpenDocumentCalled(windowId.Value, metadata.DocumentId);

        if (!this.windowDocs.TryGetValue(windowId, out var docs))
        {
            docs = [];
            this.windowDocs[windowId] = docs;
        }

        var assigned = metadata.DocumentId == Guid.Empty ? Guid.NewGuid() : metadata.DocumentId;
        docs[assigned] = metadata;
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, metadata, indexHint, shouldSelect));
        return Task.FromResult(assigned);
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
            this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
            this.LogDetached(windowId.Value, documentId);
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        this.LogDetachDocumentNotFound(windowId.Value, documentId);
        return Task.FromResult<IDocumentMetadata?>(null);
    }

    /// <inheritdoc/>
    public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        this.LogAttachDocumentCalled(targetWindowId.Value, metadata.DocumentId);

        if (!this.windowDocs.TryGetValue(targetWindowId, out var docs))
        {
            docs = [];
            this.windowDocs[targetWindowId] = docs;
        }

        docs[metadata.DocumentId] = metadata;
        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        this.LogAttached(targetWindowId.Value, metadata.DocumentId);
        return Task.FromResult(true);
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

        this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
        this.LogDocumentActivatedInvoked(windowId.Value, documentId);
        return Task.FromResult(true);
    }
}
