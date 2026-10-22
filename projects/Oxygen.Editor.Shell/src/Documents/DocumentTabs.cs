// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The tabs of a project session's windows, which Aura draws: every window of the session is bound to them. The
///     session's close coordinator decides what closing them saves.
/// </summary>
/// <param name="coordinator">The session's close coordinator.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the
///     recognition process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class DocumentTabs(DocumentCloseCoordinator coordinator, ILoggerFactory? loggerFactory = null) : IDocumentTabs, IDocumentServiceState
{
    private readonly ILogger logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<DocumentTabs>();
    private readonly DocumentCloseCoordinator coordinator = coordinator ?? throw new ArgumentNullException(nameof(coordinator));

    // Store documents per window
    private readonly Dictionary<WindowId, Dictionary<DocumentId, IDocumentMetadata>> windowDocs = [];
    private readonly Dictionary<WindowId, DocumentId> activeDocuments = [];
    private readonly HashSet<WindowId> closingWindows = [];

    // The window each detached document left, until a window attaches it.
    private readonly Dictionary<DocumentId, WindowId> detachedFrom = [];

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
    public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

    /// <inheritdoc/>
    public event EventHandler<WindowEmptiedEventArgs>? WindowEmptied;

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

        using var transaction = await this.PrepareCloseAsync(windowId, [metadata], force, emptiesWindow: true).ConfigureAwait(true);
        return transaction is not null && await transaction.CommitAsync().ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseAllAsync(WindowId windowId)
        => this.PrepareCloseAsync(windowId, this.GetOpenDocuments(windowId), force: false, emptiesWindow: false);

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentsAsync(WindowId windowId, IReadOnlyList<DocumentId> documentIds)
    {
        ArgumentNullException.ThrowIfNull(documentIds);
        return this.windowDocs.TryGetValue(windowId, out var documents) && documentIds.All(documents.ContainsKey)
            ? this.PrepareCloseAsync(windowId, [.. documentIds.Distinct().Select(id => documents[id])], force: false, emptiesWindow: false)
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
    public async Task<DocumentCloseTransaction?> PrepareCloseAllWindowsAsync(WindowId? except = null)
    {
        var prepared = new List<DocumentCloseTransaction>();
        var retained = false;
        try
        {
            foreach (var window in this.windowDocs.Keys.Where(window => window != except).ToArray())
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
    ///     The document stays open without a tab until a window attaches it; its tab is the same metadata wherever it is
    ///     attached. A pinned tab never moves, such as the scene's, and neither does a tab of a window that is closing:
    ///     their detach is refused and the tab stays where it is.
    /// </remarks>
    public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, DocumentId documentId)
    {
        if (this.closingWindows.Contains(windowId)
            || !this.windowDocs.TryGetValue(windowId, out var docs)
            || !docs.TryGetValue(documentId, out var metadata)
            || metadata.IsPinnedHint)
        {
            this.LogDetachRefused(windowId.Value, documentId);
            return Task.FromResult<IDocumentMetadata?>(null);
        }

        this.RemoveTab(windowId, documentId);
        this.detachedFrom[documentId] = windowId;
        this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
        this.LogDetached(windowId.Value, documentId);
        return Task.FromResult<IDocumentMetadata?>(metadata);
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

        // The window has the tab before it is selected, as when it opens.
        var docs = this.GetDocumentsForWindow(targetWindowId);
        docs[metadata.DocumentId] = metadata;
        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        this.LogAttached(targetWindowId.Value, metadata.DocumentId);
        if (shouldSelect)
        {
            this.activeDocuments[targetWindowId] = metadata.DocumentId;
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(targetWindowId, metadata.DocumentId));
        }

        // A drag that moved the last tab of its window leaves that window empty.
        if (this.detachedFrom.Remove(metadata.DocumentId, out var source) && source != targetWindowId && !this.windowDocs.ContainsKey(source))
        {
            this.WindowEmptied?.Invoke(this, new WindowEmptiedEventArgs(source));
        }

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

    // Only a tab's own close reports the window it empties; closing a set of tabs, or all of them, does not.
    private async Task<DocumentCloseTransaction?> PrepareCloseAsync(
        WindowId windowId, IReadOnlyList<IDocumentMetadata> documents, bool force, bool emptiesWindow)
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

            authoring = await this.coordinator.PrepareAsync(windowId, documents, force).ConfigureAwait(true);
            if (authoring is null)
            {
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

                    if (emptiesWindow && documents.Count != 0 && !this.windowDocs.ContainsKey(windowId))
                    {
                        this.WindowEmptied?.Invoke(this, new WindowEmptiedEventArgs(windowId));
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
        this.RemoveTab(windowId, documentId);
        this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(windowId, metadata));
        this.LogDocumentClosed(windowId.Value, documentId);
    }

    private void RemoveTab(WindowId windowId, DocumentId documentId)
    {
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
}
