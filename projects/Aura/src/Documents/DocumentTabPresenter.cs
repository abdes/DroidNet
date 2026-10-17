// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Presents documents inside a <see cref="Controls.TabStrip"/> by translating <see
///     cref="IDocumentService"/> lifecycle events into tab operations and mirroring user actions
///     back to the service. Each tab follows its metadata's
///     <see cref="System.ComponentModel.INotifyPropertyChanged.PropertyChanged"/>, raised on any thread, until the tab
///     closes or detaches or the presenter is disposed. It optionally hydrates existing documents when the service also
///     implements <see cref="IDocumentServiceState"/>, otherwise it starts empty and waits for new events.
/// </summary>
public sealed partial class DocumentTabPresenter : IDisposable
{
    private readonly Controls.TabStrip tabStrip;
    private readonly IDocumentService documentService;
    private readonly IManagedWindow hostWindow;
    private readonly DispatcherQueue dispatcher;
    private readonly ILogger logger;
    private readonly Dictionary<DocumentId, Controls.TabItem> tabMap = [];
    private readonly Dictionary<DocumentId, IDocumentMetadata> observed = [];

    // Documents the service has closed or detached whose tabs are still waiting for the dispatcher to remove them.
    private readonly ConcurrentDictionary<DocumentId, byte> leaving = [];
    private bool isUpdatingSelectionFromService;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentTabPresenter"/> class. Initializes
    ///     a new presenter and immediately attaches all event handlers. The constructor is
    ///     responsible for the one-shot snapshot hydration, so callers should pass in an <see
    ///     cref="IDocumentService"/> that already reflects the current state of the host window.
    /// </summary>
    /// <param name="tabStrip">The TabStrip control to present document tabs.</param>
    /// <param name="documentService">The document service supplying lifecycle events.</param>
    /// <param name="hostWindow">The host window context that owns the presented tabs.</param>
    /// <param name="dispatcher">The dispatcher used to marshal UI updates.</param>
    /// <param name="logger">Optional logger.</param>
    public DocumentTabPresenter(Controls.TabStrip tabStrip, IDocumentService documentService, IManagedWindow hostWindow, DispatcherQueue dispatcher, ILogger? logger = null)
    {
        this.tabStrip = tabStrip ?? throw new ArgumentNullException(nameof(tabStrip));
        this.documentService = documentService ?? throw new ArgumentNullException(nameof(documentService));
        this.hostWindow = hostWindow ?? throw new ArgumentNullException(nameof(hostWindow));
        this.dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        this.logger = logger ?? Microsoft.Extensions.Logging.Abstractions.NullLogger<DocumentTabPresenter>.Instance;

        // Ensure WindowId is set (redundant if MainShellView sets it, but safe)
        if (this.tabStrip.WindowId.Value == 0)
        {
            this.tabStrip.WindowId = this.hostWindow.Id;
        }

        this.documentService.DocumentOpened += this.OnDocumentOpened;
        this.documentService.DocumentClosed += this.OnDocumentClosed;
        this.documentService.DocumentDetached += this.OnDocumentDetached;
        this.documentService.DocumentAttached += this.OnDocumentAttached;
        this.documentService.DocumentActivated += this.OnDocumentActivated;

        this.tabStrip.TabCloseRequested += this.TabStrip_TabCloseRequested;
        this.tabStrip.TabDetachRequested += this.TabStrip_TabDetachRequested;
        this.tabStrip.SelectionChanged += this.TabStrip_SelectionChanged;

        this.InitializeFromServiceSnapshot();
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.documentService.DocumentOpened -= this.OnDocumentOpened;
        this.documentService.DocumentClosed -= this.OnDocumentClosed;
        this.documentService.DocumentDetached -= this.OnDocumentDetached;
        this.documentService.DocumentAttached -= this.OnDocumentAttached;
        this.documentService.DocumentActivated -= this.OnDocumentActivated;

        this.tabStrip.TabCloseRequested -= this.TabStrip_TabCloseRequested;
        this.tabStrip.TabDetachRequested -= this.TabStrip_TabDetachRequested;
        this.tabStrip.SelectionChanged -= this.TabStrip_SelectionChanged;

        foreach (var metadata in this.observed.Values)
        {
            metadata.PropertyChanged -= this.OnMetadataPropertyChanged;
        }

        this.observed.Clear();
        this.tabStrip.Items.Clear();
        this.tabMap.Clear();
    }

    private void TabStrip_SelectionChanged(object? sender, Controls.TabSelectionChangedEventArgs e)
    {
        if (this.isUpdatingSelectionFromService)
        {
            return;
        }

        if (e?.NewItem is not Controls.TabItem newItem)
        {
            return;
        }

        // Removing a tab selects a neighbor, which may belong to a document the service has already closed (closing a
        // window closes all its documents before their tabs go); that document cannot be activated.
        var documentId = DocumentId.FromValue(newItem.ContentId);
        if (this.leaving.ContainsKey(documentId))
        {
            return;
        }

        _ = this.documentService.SelectDocumentAsync(this.hostWindow.Id, documentId);
    }

    private async void TabStrip_TabCloseRequested(object? sender, Controls.TabCloseRequestedEventArgs e)
    {
        if (e is null || e.Item is null)
        {
            return;
        }

        if (e.Item is not Controls.TabItem item)
        {
            return;
        }

        var id = DocumentId.FromValue(item.ContentId);
        this.LogDocumentCloseRequested(id);
        var closed = await this.documentService.CloseDocumentAsync(this.hostWindow.Id, id, force: false).ConfigureAwait(true);
        this.LogDocumentCloseVerdict(id, closed);
    }

    private async void TabStrip_TabDetachRequested(object? sender, Controls.TabDetachRequestedEventArgs e)
    {
        if (e is null || e.Item is null)
        {
            return;
        }

        if (e.Item is not Controls.TabItem item)
        {
            return;
        }

        // A refused detach raises no event, so the tab stays where it is.
        var id = DocumentId.FromValue(item.ContentId);
        this.LogDocumentDetachRequested(id);
        _ = await this.documentService.DetachDocumentAsync(this.hostWindow.Id, id).ConfigureAwait(true);
    }

    private void OnDocumentOpened(object? sender, DocumentOpenedEventArgs e)
    {
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.dispatcher.TryEnqueue(() =>
        {
            var insertAt = this.ClampInsertIndex(e.IndexHint);
            this.AddOrUpdateTab(e.Metadata, insertAt);

            // NOTE: The attach event currently does not include selection hint (ShouldSelect),
            // so we do not adjust selection here. If your service needs to select on attach,
            // consider including a shouldSelect flag in the DocumentAttachedEventArgs.
        });
    }

    private void OnDocumentAttached(object? sender, DocumentAttachedEventArgs e)
    {
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.dispatcher.TryEnqueue(() =>
        {
            var insertAt = this.ClampInsertIndex(e.IndexHint);
            this.AddOrUpdateTab(e.Metadata, insertAt);

            // DocumentAttachedEventArgs doesn't carry a selection hint; we avoid changing
            // selection on attach. The application may choose to explicitly activate the
            // document via DocumentActivated if it wants it selected.
        });
    }

    private void OnDocumentDetached(object? sender, DocumentDetachedEventArgs e)
    {
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.leaving.TryAdd(e.Metadata.DocumentId, 0);
        _ = this.dispatcher.TryEnqueue(() =>
        {
            this.RemoveTab(e.Metadata.DocumentId);
        });
    }

    private void OnDocumentClosed(object? sender, DocumentClosedEventArgs e)
    {
        // If the service indicates a global close, apply it. Otherwise only handle for our window.
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.leaving.TryAdd(e.Metadata.DocumentId, 0);
        _ = this.dispatcher.TryEnqueue(() =>
        {
            this.RemoveTab(e.Metadata.DocumentId);
        });
    }

    private void OnDocumentActivated(object? sender, DocumentActivatedEventArgs e)
    {
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.dispatcher.TryEnqueue(() => this.ApplySelection(e.DocumentId));
    }

    private void InitializeFromServiceSnapshot()
    {
        if (this.documentService is not IDocumentServiceState stateProvider)
        {
            return;
        }

        void Hydrate()
        {
            var documents = stateProvider.GetOpenDocuments(this.hostWindow.Id);
            if (documents.Count > 0)
            {
                foreach (var metadata in documents)
                {
                    this.AddOrUpdateTab(metadata, this.tabStrip.Items.Count);
                }
            }

            this.ApplyActiveSelection(stateProvider.GetActiveDocumentId(this.hostWindow.Id));
        }

        if (this.dispatcher.HasThreadAccess)
        {
            Hydrate();
        }
        else
        {
            _ = this.dispatcher.TryEnqueue(Hydrate);
        }
    }

    private void AddOrUpdateTab(IDocumentMetadata metadata, int insertAt)
    {
        Debug.Assert(!metadata.DocumentId.IsEmpty, "Metadata must contain a valid DocumentId.");
        var docId = metadata.DocumentId;
        this.Observe(metadata);
        if (this.tabMap.TryGetValue(docId, out var existing))
        {
            TabItemExtensions.ApplyMetadataToTab(existing, metadata);
            return;
        }

        var tab = TabItemExtensions.CreateTabItemFromMetadata(docId, metadata);
        tab.Command = new AsyncRelayCommand(async ()
            => await this.documentService.SelectDocumentAsync(this.hostWindow.Id, docId)
            .ConfigureAwait(true));
        var index = this.ClampInsertIndex(insertAt);
        this.tabStrip.Items.Insert(index, tab);
        this.tabMap[docId] = tab;
    }

    private void RemoveTab(DocumentId documentId)
    {
        if (this.observed.Remove(documentId, out var metadata))
        {
            metadata.PropertyChanged -= this.OnMetadataPropertyChanged;
        }

        if (this.tabMap.Remove(documentId, out var tab))
        {
            _ = this.tabStrip.Items.Remove(tab);
        }

        _ = this.leaving.TryRemove(documentId, out _);
    }

    // A tab follows its metadata from the moment it is added until it closes or detaches.
    private void Observe(IDocumentMetadata metadata)
    {
        if (this.observed.TryGetValue(metadata.DocumentId, out var current))
        {
            if (ReferenceEquals(current, metadata))
            {
                return;
            }

            current.PropertyChanged -= this.OnMetadataPropertyChanged;
        }

        this.observed[metadata.DocumentId] = metadata;
        metadata.PropertyChanged += this.OnMetadataPropertyChanged;
    }

    // The metadata may change on any thread; the tab is updated on the strip's.
    private void OnMetadataPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is not IDocumentMetadata metadata)
        {
            return;
        }

        _ = this.dispatcher.TryEnqueue(() =>
        {
            if (this.observed.TryGetValue(metadata.DocumentId, out var current) && ReferenceEquals(current, metadata)
                && this.tabMap.TryGetValue(metadata.DocumentId, out var tab))
            {
                TabItemExtensions.ApplyMetadataToTab(tab, metadata);
            }
        });
    }

    private int ClampInsertIndex(int index)
        => index < 0 || index > this.tabStrip.Items.Count ? this.tabStrip.Items.Count : index;

    private void ApplyActiveSelection(DocumentId? documentId)
    {
        if (documentId is not { IsEmpty: false } id)
        {
            return;
        }

        this.ApplySelection(id);
    }

    private void ApplySelection(DocumentId documentId)
    {
        if (documentId.IsEmpty)
        {
            return;
        }

        if (this.tabMap.TryGetValue(documentId, out var tab))
        {
            this.isUpdatingSelectionFromService = true;
            this.tabStrip.SelectedItem = tab;
            this.isUpdatingSelectionFromService = false;
        }
    }
}
