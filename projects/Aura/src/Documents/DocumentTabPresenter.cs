// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
///     back to the service. It optionally hydrates existing documents when the service also
///     implements <see cref="IDocumentServiceState"/>, otherwise it starts empty and waits for new
///     events.
/// </summary>
public sealed partial class DocumentTabPresenter : IDisposable
{
    private readonly Controls.TabStrip tabStrip;
    private readonly IDocumentService documentService;
    private readonly IManagedWindow hostWindow;
    private readonly DispatcherQueue dispatcher;
    private readonly ILogger logger;
    private readonly Dictionary<Guid, Controls.TabItem> tabMap = [];
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
        this.documentService.DocumentMetadataChanged += this.OnDocumentMetadataChanged;
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
        this.documentService.DocumentMetadataChanged -= this.OnDocumentMetadataChanged;
        this.documentService.DocumentActivated -= this.OnDocumentActivated;

        this.tabStrip.TabCloseRequested -= this.TabStrip_TabCloseRequested;
        this.tabStrip.TabDetachRequested -= this.TabStrip_TabDetachRequested;
        this.tabStrip.SelectionChanged -= this.TabStrip_SelectionChanged;

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

        _ = this.documentService.SelectDocumentAsync(this.hostWindow.Id, newItem.ContentId);
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

        var id = item.ContentId;
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

        var id = item.ContentId;
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
            var docId = e.Metadata.DocumentId;
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

        _ = this.dispatcher.TryEnqueue(() =>
        {
            var docId = e.Metadata?.DocumentId ?? Guid.Empty;
            if (docId == Guid.Empty)
            {
                return;
            }

            if (this.tabMap.TryGetValue(docId, out var tab))
            {
                _ = this.tabStrip.Items.Remove(tab);
                _ = this.tabMap.Remove(docId);
            }
        });
    }

    private void OnDocumentClosed(object? sender, DocumentClosedEventArgs e)
    {
        // If the service indicates a global close, apply it. Otherwise only handle for our window.
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.dispatcher.TryEnqueue(() =>
        {
            var docId = e.Metadata?.DocumentId ?? Guid.Empty;
            if (docId == Guid.Empty)
            {
                return;
            }

            if (this.tabMap.TryGetValue(docId, out var tab))
            {
                _ = this.tabStrip.Items.Remove(tab);
                _ = this.tabMap.Remove(docId);
            }
        });
    }

    private void OnDocumentMetadataChanged(object? sender, DocumentMetadataChangedEventArgs e)
    {
        if (e.WindowId != this.hostWindow.Id)
        {
            return;
        }

        _ = this.dispatcher.TryEnqueue(() =>
        {
            var docId = e.NewMetadata?.DocumentId ?? Guid.Empty;
            if (docId == Guid.Empty)
            {
                return;
            }

            if (this.tabMap.TryGetValue(docId, out var tab))
            {
                TabItemExtensions.ApplyMetadataToTab(tab, e.NewMetadata);
            }
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
        Debug.Assert(metadata.DocumentId != Guid.Empty, "Metadata must contain a valid DocumentId.");
        var docId = metadata.DocumentId;
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

    private int ClampInsertIndex(int index)
        => index < 0 || index > this.tabStrip.Items.Count ? this.tabStrip.Items.Count : index;

    private void ApplyActiveSelection(Guid? documentId)
    {
        if (documentId is null || documentId == Guid.Empty)
        {
            return;
        }

        this.ApplySelection(documentId.Value);
    }

    private void ApplySelection(Guid documentId)
    {
        if (documentId == Guid.Empty)
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
