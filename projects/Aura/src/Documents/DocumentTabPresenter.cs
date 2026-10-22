// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Windowing;
using DroidNet.Coordinates;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Dispatching;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Presents documents inside a <see cref="Controls.TabStrip"/> by translating <see
///     cref="IDocumentTabSource"/> lifecycle events into tab operations and mirroring user actions
///     back to the service. Each tab follows its metadata's
///     <see cref="System.ComponentModel.INotifyPropertyChanged.PropertyChanged"/>, raised on any thread, until the tab
///     closes or detaches or the presenter is disposed. It optionally hydrates existing documents when the service also
///     implements <see cref="IDocumentServiceState"/>, otherwise it starts empty and waits for new events.
/// </summary>
/// <remarks>
///     With the application's <see cref="DocumentTransfers"/>, a tab dragged out of the strip detaches its document,
///     which the strip it is dropped on attaches, in this window or another; a tab dropped outside every strip attaches
///     its document to the window <see cref="IDocumentWindowFactory"/> creates for it. Only the tab source that detached
///     a document attaches it again: a strip whose tabs come from another source does not take it. A document that no
///     window takes goes back to the window it came from. The presenter owns the strip's items: it removes the tab a drag inserted
///     while passing over the strip, and adopts the one it dropped.
/// </remarks>
public sealed partial class DocumentTabPresenter : IDisposable
{
    private readonly Controls.TabStrip tabStrip;
    private readonly IDocumentTabSource tabSource;
    private readonly IManagedWindow hostWindow;
    private readonly DispatcherQueue dispatcher;
    private readonly ILogger logger;
    private readonly DocumentTransfers? transfers;
    private readonly Dictionary<DocumentId, Controls.TabItem> tabMap = [];
    private readonly Dictionary<DocumentId, IDocumentMetadata> observed = [];

    // Documents the service has closed or detached whose tabs are still waiting for the dispatcher to remove them.
    private readonly ConcurrentDictionary<DocumentId, byte> leaving = [];
    private bool isUpdatingSelectionFromService;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentTabPresenter"/> class. Initializes
    ///     a new presenter and immediately attaches all event handlers. The constructor is
    ///     responsible for the one-shot snapshot hydration, so callers should pass in an <see
    ///     cref="IDocumentTabSource"/> that already reflects the current state of the host window.
    /// </summary>
    /// <param name="tabStrip">The TabStrip control to present document tabs.</param>
    /// <param name="tabSource">The document service supplying lifecycle events.</param>
    /// <param name="hostWindow">The host window context that owns the presented tabs.</param>
    /// <param name="dispatcher">The dispatcher used to marshal UI updates.</param>
    /// <param name="logger">Optional logger.</param>
    /// <param name="transfers">
    ///     The documents tab drags move between windows; without them, a tab dragged out of the strip keeps its document.
    /// </param>
    public DocumentTabPresenter(Controls.TabStrip tabStrip, IDocumentTabSource tabSource, IManagedWindow hostWindow, DispatcherQueue dispatcher, ILogger? logger = null, DocumentTransfers? transfers = null)
    {
        this.tabStrip = tabStrip ?? throw new ArgumentNullException(nameof(tabStrip));
        this.tabSource = tabSource ?? throw new ArgumentNullException(nameof(tabSource));
        this.hostWindow = hostWindow ?? throw new ArgumentNullException(nameof(hostWindow));
        this.dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        this.logger = logger ?? Microsoft.Extensions.Logging.Abstractions.NullLogger<DocumentTabPresenter>.Instance;
        this.transfers = transfers;

        // Ensure WindowId is set (redundant if MainShellView sets it, but safe)
        if (this.tabStrip.WindowId.Value == 0)
        {
            this.tabStrip.WindowId = this.hostWindow.Id;
        }

        this.tabSource.DocumentOpened += this.OnDocumentOpened;
        this.tabSource.DocumentClosed += this.OnDocumentClosed;
        this.tabSource.DocumentDetached += this.OnDocumentDetached;
        this.tabSource.DocumentAttached += this.OnDocumentAttached;
        this.tabSource.DocumentActivated += this.OnDocumentActivated;

        this.tabStrip.TabCloseRequested += this.TabStrip_TabCloseRequested;
        this.tabStrip.TabDetachRequested += this.TabStrip_TabDetachRequested;
        this.tabStrip.TabDragComplete += this.TabStrip_TabDragComplete;
        this.tabStrip.TabTearOutRequested += this.TabStrip_TabTearOutRequested;
        this.tabStrip.SelectionChanged += this.TabStrip_SelectionChanged;

        this.InitializeFromServiceSnapshot();
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.tabSource.DocumentOpened -= this.OnDocumentOpened;
        this.tabSource.DocumentClosed -= this.OnDocumentClosed;
        this.tabSource.DocumentDetached -= this.OnDocumentDetached;
        this.tabSource.DocumentAttached -= this.OnDocumentAttached;
        this.tabSource.DocumentActivated -= this.OnDocumentActivated;

        this.tabStrip.TabCloseRequested -= this.TabStrip_TabCloseRequested;
        this.tabStrip.TabDetachRequested -= this.TabStrip_TabDetachRequested;
        this.tabStrip.TabDragComplete -= this.TabStrip_TabDragComplete;
        this.tabStrip.TabTearOutRequested -= this.TabStrip_TabTearOutRequested;
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
        // A tab that a drag passing over the strip inserted has no document here yet.
        var documentId = DocumentId.FromValue(newItem.ContentId);
        if (this.leaving.ContainsKey(documentId) || !this.tabMap.ContainsKey(documentId))
        {
            return;
        }

        _ = this.tabSource.SelectDocumentAsync(this.hostWindow.Id, documentId);
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
        var closed = await this.tabSource.CloseDocumentAsync(this.hostWindow.Id, id, force: false).ConfigureAwait(true);
        this.LogDocumentCloseVerdict(id, closed);
    }

    private async void TabStrip_TabDetachRequested(object? sender, Controls.TabDetachRequestedEventArgs e)
    {
        if (e?.Item is not Controls.TabItem item)
        {
            return;
        }

        // The drag leaves a strip it was passing over with the tab it inserted there, which has no document here.
        var id = DocumentId.FromValue(item.ContentId);
        if (!this.tabMap.ContainsKey(id))
        {
            this.RemoveDroppedTab(id);
            return;
        }

        // Nobody could attach the document, so it keeps its tab. A refused detach raises no event either.
        if (this.transfers is null)
        {
            this.LogDocumentDetachUnavailable(id);
            return;
        }

        this.LogDocumentDetachRequested(id);
        if (await this.tabSource.DetachDocumentAsync(this.hostWindow.Id, id).ConfigureAwait(true) is { } document)
        {
            this.transfers.Hold(document, this.hostWindow.Id, this.tabSource);
        }
    }

    // A drag ends on the strip it was dropped on, or on its source strip when it was dropped elsewhere. A document the
    // drag moved is attached here when it was dropped here, and goes back where it came from otherwise; a tab that only
    // moved within the strip moved no document.
    private async void TabStrip_TabDragComplete(object? sender, Controls.TabDragCompleteEventArgs e)
    {
        if (e?.Item is not Controls.TabItem item
            || this.transfers is not { } transfers
            || transfers.Take(DocumentId.FromValue(item.ContentId)) is not { } transfer)
        {
            return;
        }

        var dropped = ReferenceEquals(e.DestinationStrip, this.tabStrip);
        await this.AttachAsync(transfer, dropped ? this.hostWindow.Id : transfer.Source, dropped ? (e.NewIndex ?? -1) : -1)
            .ConfigureAwait(true);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A window that fails to open leaves the document where it came from.")]
    private async void TabStrip_TabTearOutRequested(object? sender, Controls.TabTearOutRequestedEventArgs e)
    {
        if (e?.Item is not Controls.TabItem item
            || this.transfers is not { } transfers
            || transfers.Take(DocumentId.FromValue(item.ContentId)) is not { } transfer)
        {
            return;
        }

        WindowId? window = null;
        try
        {
            var position = new SpatialMapper(this.hostWindow.Window, this.tabStrip).ToPhysicalScreen(e.ScreenDropPoint.AsScreen());
            window = await transfers.CreateWindowAsync(transfer.Document, position).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.LogDocumentWindowFailed(transfer.Document.DocumentId, exception);
        }

        await this.AttachAsync(transfer, window ?? transfer.Source, indexHint: -1).ConfigureAwait(true);
    }

    // Attaches a moved document to a window through the tab source that detached it; a document that the window refuses,
    // or that this strip's tabs cannot take because they come from another source, goes back where it came from.
    private async Task AttachAsync(DocumentTransfer transfer, WindowId target, int indexHint)
    {
        var document = transfer.Document;
        var accepted = target != this.hostWindow.Id || ReferenceEquals(transfer.Tabs, this.tabSource);
        if (accepted)
        {
            this.LogDocumentAttachRequested(document.DocumentId, target.Value);
            if (await transfer.Tabs.AttachDocumentAsync(target, document, indexHint, shouldSelect: true).ConfigureAwait(true))
            {
                return;
            }
        }

        if (target == this.hostWindow.Id)
        {
            this.RemoveDroppedTab(document.DocumentId);
        }

        if (target != transfer.Source)
        {
            this.LogDocumentReturned(document.DocumentId, transfer.Source.Value);
            _ = await transfer.Tabs.AttachDocumentAsync(transfer.Source, document, indexHint: -1, shouldSelect: true).ConfigureAwait(true);
        }
    }

    // Removes a tab that a drag inserted in the strip without a document of this window behind it.
    private void RemoveDroppedTab(DocumentId documentId)
    {
        if (!this.tabMap.ContainsKey(documentId)
            && this.tabStrip.Items.FirstOrDefault(tab => tab.ContentId == documentId.Value) is { } dropped)
        {
            _ = this.tabStrip.Items.Remove(dropped);
        }
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
        // A drag that ended in another window can leave here the tab it inserted while passing over this strip.
        if (e.WindowId != this.hostWindow.Id)
        {
            _ = this.dispatcher.TryEnqueue(() => this.RemoveDroppedTab(e.Metadata.DocumentId));
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
        if (this.tabSource is not IDocumentServiceState stateProvider)
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

        // A document a drag dropped here keeps the tab the drag inserted where it was dropped.
        var dropped = this.tabStrip.Items.FirstOrDefault(item => item.ContentId == docId.Value);
        var tab = dropped ?? TabItemExtensions.CreateTabItemFromMetadata(docId, metadata);
        if (dropped is not null)
        {
            TabItemExtensions.ApplyMetadataToTab(dropped, metadata);
        }

        // A drag copies the tab with its command into the strips it passes over; there, the document is not this
        // window's.
        tab.Command = new AsyncRelayCommand(async () =>
        {
            if (this.tabMap.ContainsKey(docId))
            {
                _ = await this.tabSource.SelectDocumentAsync(this.hostWindow.Id, docId).ConfigureAwait(true);
            }
        });
        if (dropped is null)
        {
            this.tabStrip.Items.Insert(this.ClampInsertIndex(insertAt), tab);
        }

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
