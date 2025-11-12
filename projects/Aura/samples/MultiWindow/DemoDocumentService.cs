// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Windowing;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// Minimal in-sample document service for the MultiWindow demo. Implements enough of
/// the IDocumentService contract to support opening, selecting, updating and closing
/// documents; it's intentionally small and not intended for production.
/// </summary>
public sealed class DemoDocumentService : IDocumentService
{
    private readonly ConcurrentDictionary<Guid, IDocumentMetadata> docs = new();

    public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

    public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

    public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

    public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

    public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

    public event EventHandler<DocumentMetadataChangedEventArgs>? DocumentMetadataChanged;

    public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

    public Task<Guid> OpenDocumentAsync(WindowContext window, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        var id = metadata?.DocumentId ?? Guid.NewGuid();
        var meta = metadata ?? new DemoDocumentMetadata { DocumentId = id, Title = "Untitled" };
        var assigned = meta.DocumentId == Guid.Empty ? id : meta.DocumentId;
        this.docs[assigned] = meta;

        Debug.WriteLine($"DemoDocumentService.OpenDocumentAsync: window={(window is not null ? window.Id.Value.ToString(CultureInfo.InvariantCulture) : "null")}, DocumentId={assigned}, Title='{meta.Title}', ShouldSelect={shouldSelect}");
        this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(window!, meta, indexHint, shouldSelect));
        return Task.FromResult(assigned);
    }

    public Task<bool> CloseDocumentAsync(Guid documentId, bool force = false)
    {
        Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId}, Force={force}");

        if (!this.docs.TryRemove(documentId, out var metadata))
        {
            Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId} not found");
            return Task.FromResult(false);
        }

        // Raise DocumentClosed with dummy WindowContext of the first match; demo doesn't track per-window lists
        Debug.WriteLine($"DemoDocumentService.CloseDocumentAsync: DocumentId={documentId} - closed, raising DocumentClosed");
        this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(window: null!, metadata));
        return Task.FromResult(true);
    }

    public Task<IDocumentMetadata?> DetachDocumentAsync(Guid documentId)
    {
        if (this.docs.TryRemove(documentId, out var metadata))
        {
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        return Task.FromResult<IDocumentMetadata?>(null);
    }

    public Task<bool> AttachDocumentAsync(WindowContext targetWindow, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
    {
        this.docs[metadata.DocumentId] = metadata;
        this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindow, metadata, indexHint));
        return Task.FromResult(true);
    }

    public Task<bool> UpdateMetadataAsync(Guid documentId, IDocumentMetadata metadata)
    {
        if (!this.docs.ContainsKey(documentId))
        {
            return Task.FromResult(false);
        }

        this.docs[documentId] = metadata;

        // For sample, we raise DocumentMetadataChanged without a WindowContext; real apps will pass proper context
        this.DocumentMetadataChanged?.Invoke(this, new DocumentMetadataChangedEventArgs(null!, metadata));
        return Task.FromResult(true);
    }

    public Task<bool> SelectDocumentAsync(WindowContext window, Guid documentId)
    {
        if (!this.docs.TryGetValue(documentId, out var metadata))
        {
            return Task.FromResult(false);
        }

        this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(window, documentId));
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
    }
}
