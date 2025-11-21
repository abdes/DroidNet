// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Windowing;
using Microsoft.UI;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// Minimal in-sample document service for the MultiWindow demo. Implements enough of
/// the IDocumentService contract to support opening, selecting, updating and closing
/// documents; it's intentionally small and not intended for production.
/// </summary>
[SuppressMessage("Usage", "CA1812:AvoidUninstantiatedInternalClasses", Justification = "Instantiated via DI")]
internal sealed class DemoDocumentService : IDocumentService
{
    private readonly ConcurrentDictionary<Guid, IDocumentMetadata> docs = new();

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
        var id = metadata?.DocumentId ?? Guid.NewGuid();
        var meta = metadata ?? new DemoDocumentMetadata { DocumentId = id, Title = "Untitled" };
        var assigned = meta.DocumentId == Guid.Empty ? id : meta.DocumentId;
        this.docs[assigned] = meta;

        Debug.WriteLine($"DemoDocumentService.OpenDocumentAsync: window={windowId.Value}, DocumentId={assigned}, Title='{meta.Title}', ShouldSelect={shouldSelect}");
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, meta, indexHint, shouldSelect));
        return Task.FromResult(assigned);
    }

    /// <inheritdoc/>
    public async Task<bool> CloseDocumentAsync(WindowId windowId, Guid documentId, bool force = false)
    {
        Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId}, Force={force}");

        if (!this.docs.TryGetValue(documentId, out var metadata))
        {
            Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId} not found");
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
                Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId} vetoed");
                return false;
            }
        }

        // Proceed with close
        _ = this.docs.TryRemove(documentId, out _);
        Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId} - closed, raising DocumentClosed");
        this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(windowId, metadata));
        return true;
    }

    /// <inheritdoc/>
    public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, Guid documentId)
    {
        if (this.docs.TryRemove(documentId, out var metadata))
        {
            this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        return Task.FromResult<IDocumentMetadata?>(null);
    }

    /// <inheritdoc/>
    public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        this.docs[metadata.DocumentId] = metadata;
        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> UpdateMetadataAsync(WindowId windowId, Guid documentId, IDocumentMetadata metadata)
    {
        if (!this.docs.ContainsKey(documentId))
        {
            return Task.FromResult(false);
        }

        this.docs[documentId] = metadata;

        // For sample, raise DocumentMetadataChanged with the provided window.
        this.DocumentMetadataChanged?.Invoke(this, new DocumentMetadataChangedEventArgs(windowId, metadata));
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> SelectDocumentAsync(WindowId windowId, Guid documentId)
    {
        if (!this.docs.TryGetValue(documentId, out var metadata))
        {
            return Task.FromResult(false);
        }

        this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
        return Task.FromResult(true);
    }

    // Minimal metadata type used only by the sample
    private sealed class DemoDocumentMetadata : IDocumentMetadata
    {
        public Guid DocumentId { get; set; } = Guid.NewGuid();

        public string Title { get; set; } = string.Empty;

        public Uri? IconUri { get; set; }

        public bool IsDirty { get; set; }

        public bool IsPinnedHint { get; set; }

        public bool IsClosable { get; set; } = true;
    }
}
