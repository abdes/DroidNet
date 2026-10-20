// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using Microsoft.UI;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     Minimal in-sample document tab source for the MultiWindow demo. Implements enough of the
///     IDocumentTabSource contract to support opening, selecting and closing documents;
///     it's intentionally small and not intended for production.
/// </summary>
[SuppressMessage("Usage", "CA1812:AvoidUninstantiatedInternalClasses", Justification = "Instantiated via DI")]
internal sealed class DemoDocumentTabSource : IDocumentTabSource, IDocumentServiceState
{
    private readonly Dictionary<WindowId, Dictionary<DocumentId, IDocumentMetadata>> windowDocs = [];
    private readonly Dictionary<WindowId, DocumentId> activeDocuments = [];

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
    public Task<bool> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        var id = metadata.DocumentId;
        var docs = this.GetOrCreateWindowDocuments(windowId);
        docs[id] = metadata;

        Debug.WriteLine($"DemoDocumentTabSource.OpenDocumentAsync: window={windowId.Value}, DocumentId={id}, Title='{metadata.Title}', ShouldSelect={shouldSelect}");
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, metadata, indexHint, shouldSelect));
        if (shouldSelect)
        {
            this.activeDocuments[windowId] = id;
        }

        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public async Task<bool> CloseDocumentAsync(WindowId windowId, DocumentId documentId, bool force = false)
    {
        Debug.WriteLine($"DemoDocumentTabSource.CloseDocumentAsync: DocumentId={documentId}, Force={force}");

        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.TryGetValue(documentId, out var metadata))
        {
            Debug.WriteLine($"DemoDocumentTabSource.CloseDocumentAsync: DocumentId={documentId} not found");
            return false;
        }

        // Fire DocumentClosing and await veto if not forced
        var closingArgs = new DocumentClosingEventArgs(windowId, metadata, force);
        this.DocumentClosing?.Invoke(this, closingArgs);

        if (!force)
        {
            var allowed = await closingArgs.WaitForVetoResultAsync().ConfigureAwait(false);
            if (!allowed)
            {
                Debug.WriteLine($"DemoDocumentTabSource.CloseDocumentAsync: DocumentId={documentId} vetoed");
                return false;
            }
        }

        // Proceed with close
        _ = docs.Remove(documentId);
        if (docs.Count == 0)
        {
            _ = this.windowDocs.Remove(windowId);
        }

        if (this.activeDocuments.TryGetValue(windowId, out var activeId) && activeId == documentId)
        {
            _ = this.activeDocuments.Remove(windowId);
        }

        Debug.WriteLine($"DemoDocumentTabSource.CloseDocumentAsync: DocumentId={documentId} - closed, raising DocumentClosed");
        this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(windowId, metadata));
        return true;
    }

    /// <inheritdoc/>
    public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, DocumentId documentId)
    {
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
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        return Task.FromResult<IDocumentMetadata?>(null);
    }

    /// <inheritdoc/>
    public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        var docs = this.GetOrCreateWindowDocuments(targetWindowId);
        docs[metadata.DocumentId] = metadata;
        if (shouldSelect)
        {
            this.activeDocuments[targetWindowId] = metadata.DocumentId;
        }

        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> SelectDocumentAsync(WindowId windowId, DocumentId documentId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs) || !docs.ContainsKey(documentId))
        {
            return Task.FromResult(false);
        }

        this.activeDocuments[windowId] = documentId;
        this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
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

    private Dictionary<DocumentId, IDocumentMetadata> GetOrCreateWindowDocuments(WindowId windowId)
    {
        if (!this.windowDocs.TryGetValue(windowId, out var docs))
        {
            docs = [];
            this.windowDocs[windowId] = docs;
        }

        return docs;
    }
}
