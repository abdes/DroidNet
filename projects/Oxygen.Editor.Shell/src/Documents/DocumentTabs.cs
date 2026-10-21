// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The tabs of every window, which Aura draws. The project session's close coordinator decides what closes save.
/// </summary>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the
///     recognition process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class DocumentTabs(ILoggerFactory? loggerFactory = null) : IDocumentTabs, IDocumentServiceState
{
    private readonly ILogger logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<DocumentTabs>();

    // Store documents per window
    private readonly Dictionary<WindowId, Dictionary<DocumentId, IDocumentMetadata>> windowDocs = [];
    private readonly Dictionary<WindowId, DocumentId> activeDocuments = [];
    private readonly HashSet<WindowId> closingWindows = [];
    private DocumentCloseCoordinator? coordinator;

    /// <inheritdoc/>
    public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

    /// <inheritdoc/>
    public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

    /// <inheritdoc/>
    public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

    /// <inheritdoc/>
    /// <remarks>Never raised: every detach is refused until the editor has document windows to receive a tab.</remarks>
    public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached
    {
        add { }
        remove { }
    }

    /// <inheritdoc/>
    public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

    /// <inheritdoc/>
    public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

    /// <inheritdoc/>
    /// <remarks>A document has at most one tab: opening a tab for a document that has one, in any window, is refused.</remarks>
    public Task<bool> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        if (this.closingWindows.Contains(windowId))
        {
            return Task.FromResult(false);
        }

        var documentId = metadata.DocumentId;
        this.LogOpenDocumentCalled(windowId.Value, documentId);

        if (this.HasTab(documentId))
        {
            this.LogDocumentOpenAborted(windowId.Value, documentId);
            return Task.FromResult(false);
        }

        var docs = this.GetDocumentsForWindow(windowId);
        docs[documentId] = metadata;
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, metadata, indexHint, shouldSelect));
        if (shouldSelect)
        {
            this.activeDocuments[windowId] = documentId;

            // Raise DocumentActivated for consistency with SelectDocumentAsync
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
        }

        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public async Task<bool> CloseDocumentAsync(WindowId windowId, DocumentId documentId, bool force = false)
    {
        this.LogCloseDocumentCalled(windowId.Value, documentId, force);
        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.TryGetValue(documentId, out var metadata))
        {
            this.LogCloseDocumentNotFound(windowId.Value, documentId);
            return false;
        }

        using var transaction = await this.PrepareCloseAsync(windowId, [metadata], force).ConfigureAwait(true);
        return transaction is not null && await transaction.CommitAsync().ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseAllAsync(WindowId windowId)
        => this.PrepareCloseAsync(windowId, this.GetOpenDocuments(windowId), force: false);

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentsAsync(WindowId windowId, IReadOnlyList<DocumentId> documentIds)
    {
        ArgumentNullException.ThrowIfNull(documentIds);
        return this.windowDocs.TryGetValue(windowId, out var documents) && documentIds.All(documents.ContainsKey)
            ? this.PrepareCloseAsync(windowId, [.. documentIds.Distinct().Select(id => documents[id])], force: false)
            : Task.FromResult<DocumentCloseTransaction?>(null);
    }

    /// <inheritdoc/>
    public WindowId? FindWindow(DocumentId documentId)
    {
        foreach (var (window, documents) in this.windowDocs)
        {
            if (documents.ContainsKey(documentId))
            {
                return window;
            }
        }

        return null;
    }

    /// <inheritdoc/>
    public IDisposable UseCloseCoordinator(DocumentCloseCoordinator coordinator)
    {
        ArgumentNullException.ThrowIfNull(coordinator);
        if (this.coordinator is not null)
        {
            throw new InvalidOperationException("Another close coordinator is in use.");
        }

        this.coordinator = coordinator;
        return new CoordinatorUse(this, coordinator);
    }

    /// <inheritdoc/>
    public async Task<DocumentCloseTransaction?> PrepareCloseAllWindowsAsync()
    {
        var prepared = new List<DocumentCloseTransaction>();
        var retained = false;
        try
        {
            foreach (var window in this.windowDocs.Keys.ToArray())
            {
                var transaction = await this.PrepareCloseAllAsync(window).ConfigureAwait(true);
                if (transaction is null)
                {
                    return null;
                }

                prepared.Add(transaction);
            }

            retained = true;
            return new DocumentCloseTransaction(
                async () =>
                {
                    foreach (var transaction in prepared)
                    {
                        if (!await transaction.CommitAsync().ConfigureAwait(true))
                        {
                            return false;
                        }
                    }

                    return true;
                },
                () => prepared.ForEach(transaction => transaction.Dispose()));
        }
        finally
        {
            if (!retained)
            {
                prepared.ForEach(transaction => transaction.Dispose());
            }
        }
    }

    /// <inheritdoc/>
    /// <remarks>
    ///     Always refused: a torn-out tab needs a document window to receive it, which the editor does not have yet. The
    ///     tab stays where it is.
    /// </remarks>
    public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, DocumentId documentId)
    {
        this.LogDetachRefused(windowId.Value, documentId);
        return Task.FromResult<IDocumentMetadata?>(null);
    }

    /// <inheritdoc/>
    /// <remarks>A document has at most one tab: attaching a document that has one, in any window, is refused.</remarks>
    public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        if (this.closingWindows.Contains(targetWindowId))
        {
            return Task.FromResult(false);
        }

        this.LogAttachDocumentCalled(targetWindowId.Value, metadata.DocumentId);

        if (this.HasTab(metadata.DocumentId))
        {
            this.LogAttachDocumentAborted(targetWindowId.Value, metadata.DocumentId);
            return Task.FromResult(false);
        }

        var docs = this.GetDocumentsForWindow(targetWindowId);
        docs[metadata.DocumentId] = metadata;
        if (shouldSelect)
        {
            this.activeDocuments[targetWindowId] = metadata.DocumentId;

            // Raise DocumentActivated for consistency with SelectDocumentAsync
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(targetWindowId, metadata.DocumentId));
        }

        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        this.LogAttached(targetWindowId.Value, metadata.DocumentId);
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> SelectDocumentAsync(WindowId windowId, DocumentId documentId)
    {
        if (this.closingWindows.Contains(windowId))
        {
            return Task.FromResult(false);
        }

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
    public DocumentId? GetActiveDocumentId(WindowId windowId)
        => this.activeDocuments.TryGetValue(windowId, out var documentId) && !documentId.IsEmpty
            ? documentId
            : null;

    private async Task<DocumentCloseTransaction?> PrepareCloseAsync(
        WindowId windowId, IReadOnlyList<IDocumentMetadata> documents, bool force)
    {
        if (!this.closingWindows.Add(windowId))
        {
            return null;
        }

        var retained = false;
        DocumentCloseTransaction? authoring = null;
        try
        {
            if (!await this.ConfirmVetoesAsync(windowId, documents, force).ConfigureAwait(true))
            {
                return null;
            }

            if (this.coordinator is { } coordinator)
            {
                authoring = await coordinator.PrepareAsync(windowId, documents, force).ConfigureAwait(true);
                if (authoring is null)
                {
                    return null;
                }
            }
            else if (!force && documents.Any(metadata => metadata.IsDirty))
            {
                // Applications must install a close policy before dirty documents can be closed.
                return null;
            }

            retained = true;
            return new DocumentCloseTransaction(
                async () =>
                {
                    if (authoring is not null && !await authoring.CommitAsync().ConfigureAwait(true))
                    {
                        return false;
                    }

                    foreach (var metadata in documents)
                    {
                        this.RemoveClosedDocument(windowId, metadata);
                    }

                    return true;
                },
                () =>
                {
                    authoring?.Dispose();
                    _ = this.closingWindows.Remove(windowId);
                });
        }
        finally
        {
            if (!retained)
            {
                authoring?.Dispose();
                _ = this.closingWindows.Remove(windowId);
            }
        }
    }

    private async Task<bool> ConfirmVetoesAsync(WindowId windowId, IReadOnlyList<IDocumentMetadata> documents, bool force)
    {
        foreach (var metadata in documents)
        {
            var args = new DocumentClosingEventArgs(windowId, metadata, force);
            this.DocumentClosing?.Invoke(this, args);
            if (!force && !await args.WaitForVetoResultAsync().ConfigureAwait(true))
            {
                return false;
            }
        }

        return true;
    }

    private void RemoveClosedDocument(WindowId windowId, IDocumentMetadata metadata)
    {
        var documentId = metadata.DocumentId;
        var docs = this.windowDocs[windowId];
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
    }

    private bool HasTab(DocumentId documentId) => this.FindWindow(documentId) is not null;

    private Dictionary<DocumentId, IDocumentMetadata> GetDocumentsForWindow(WindowId windowId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs))
        {
            docs = [];
            this.windowDocs[windowId] = docs;
        }

        return docs;
    }

    private sealed partial class CoordinatorUse(DocumentTabs tabs, DocumentCloseCoordinator coordinator) : IDisposable
    {
        public void Dispose()
        {
            if (ReferenceEquals(tabs.coordinator, coordinator))
            {
                tabs.coordinator = null;
            }
        }
    }
}
