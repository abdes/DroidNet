// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The document area of one window, over the tabs of <see cref="IDocumentTabs"/> in that window.
/// </summary>
/// <remarks>
///     Every document of <see cref="OpenDocuments"/> that gets a tab here gets a <see cref="DocumentTab"/>. When that tab
///     closes, by the user or by <see cref="CloseAsync"/>, the area lets go of its editor, disposes the tab and closes the
///     document; the session's document service then disposes the editor. A tab that a drag moves to another window
///     leaves the area with its document open, and the area of that window adopts the same tab.
/// </remarks>
public sealed partial class DocumentArea : IDocumentArea, IDisposable
{
    private readonly IDocumentTabs tabs;
    private readonly OpenDocuments documents;
    private readonly DocumentEditors editors;
    private readonly IDocumentService service;
    private readonly IDocumentFiles files;
    private readonly IOperationResultPublisher operationResults;
    private readonly WindowId window;
    private readonly Action<WindowId> bringForward;
    private readonly ILogger logger;
    private readonly List<DocumentTab> shown = [];

    [SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "An open in flight releases the gate after Dispose; the gate holds no wait handle.")]
    private readonly SemaphoreSlim placements = new(1, 1);

    private long replacement;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentArea"/> class.
    /// </summary>
    /// <param name="tabs">The service that holds the tabs of every window.</param>
    /// <param name="documents">The open documents of the session.</param>
    /// <param name="editors">The editor of each kind the session opens.</param>
    /// <param name="service">The session's document service, which owns the documents' editors and saves them.</param>
    /// <param name="files">Where the project keeps the files that documents edit.</param>
    /// <param name="operationResults">Publishes the failures of opens.</param>
    /// <param name="window">The window whose document area this is.</param>
    /// <param name="bringForward">Brings a window to the foreground.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public DocumentArea(
        IDocumentTabs tabs,
        OpenDocuments documents,
        DocumentEditors editors,
        IDocumentService service,
        IDocumentFiles files,
        IOperationResultPublisher operationResults,
        WindowId window,
        Action<WindowId> bringForward,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(tabs);
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(editors);
        ArgumentNullException.ThrowIfNull(service);
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(operationResults);
        ArgumentNullException.ThrowIfNull(bringForward);
        this.tabs = tabs;
        this.documents = documents;
        this.editors = editors;
        this.service = service;
        this.files = files;
        this.operationResults = operationResults;
        this.window = window;
        this.bringForward = bringForward;
        this.logger = loggerFactory?.CreateLogger<DocumentArea>() ?? NullLogger<DocumentArea>.Instance;

        this.tabs.DocumentOpened += this.OnTabOpened;
        this.tabs.DocumentAttached += this.OnTabAttached;
        this.tabs.DocumentActivated += this.OnTabActivated;
        this.tabs.DocumentDetached += this.OnTabDetached;
        this.tabs.DocumentClosed += this.OnTabClosed;
    }

    /// <inheritdoc/>
    public event EventHandler? ActiveDocumentChanged;

    /// <inheritdoc/>
    public event EventHandler? ActiveEditorChanged;

    /// <inheritdoc/>
    public event EventHandler<DocumentPlacedEventArgs>? Placed;

    /// <inheritdoc/>
    public event EventHandler? DocumentsChanged;

    /// <summary>
    ///     Raised when a document's tab is added to this area, opened or moved here, before it is selected.
    /// </summary>
    internal event EventHandler<DocumentTabEventArgs>? TabAdded;

    /// <summary>
    ///     Raised when a document's tab leaves this area: before the document closes, or once a drag took it away.
    /// </summary>
    internal event EventHandler<DocumentTabEventArgs>? TabRemoved;

    /// <inheritdoc/>
    public IReadOnlyList<Document> Documents => [.. this.shown.Select(static tab => tab.Document)];

    /// <inheritdoc/>
    public Document? ActiveDocument { get; private set; }

    /// <inheritdoc/>
    public object? ActiveEditor { get; private set; }

    /// <summary>
    ///     Gets the window whose document area this is.
    /// </summary>
    internal WindowId Window => this.window;

    /// <inheritdoc/>
    public async Task<DocumentPlacement> OpenAsync(AssetPath asset, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(asset);
        ObjectDisposedException.ThrowIf(this.disposed, this);
        var placement = await this.PlaceAsync(asset, cancellationToken).ConfigureAwait(true);
        if (placement.Problem is { } problem)
        {
            this.LogNotOpened(asset, problem);
            this.PublishOpenFailure(placement, problem);
        }

        this.Placed?.Invoke(this, new DocumentPlacedEventArgs(placement));
        return placement;
    }

    /// <inheritdoc/>
    public async Task<bool> ShowAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        ObjectDisposedException.ThrowIf(this.disposed, this);
        if (!this.documents.Documents.Contains(document))
        {
            throw new ArgumentException($"'{document.Title}' is not open.", nameof(document));
        }

        // A document has one tab: when another window has it, that window shows it.
        if (this.tabs.FindWindow(document.Id) is { } owner)
        {
            return await this.SelectAsync(owner, document).ConfigureAwait(true);
        }

        var tab = new DocumentTab(document, this.editors.Of(document.Kind));
        this.shown.Add(tab);
        if (await this.tabs.OpenDocumentAsync(this.window, tab).ConfigureAwait(true))
        {
            this.bringForward(this.window);
            return true;
        }

        this.LogShowRefused(document.Title);
        _ = this.shown.Remove(tab);
        tab.Dispose();
        return false;
    }

    /// <inheritdoc/>
    public async Task<bool> SaveAllAsync()
    {
        foreach (var document in this.Documents.Where(static document => document.IsDirty))
        {
            if (!await this.service.SaveAsync(document).ConfigureAwait(true))
            {
                return false;
            }
        }

        return true;
    }

    /// <inheritdoc/>
    public Task<bool> SaveBeforeAsync(
        SaveBeforeOperation operation,
        Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>> findUnsaved,
        CancellationToken cancellationToken = default)
        => this.service.SaveBeforeAsync(this.window, operation, findUnsaved, cancellationToken);

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseAsync(IReadOnlyList<Document> documents)
    {
        ArgumentNullException.ThrowIfNull(documents);
        return this.tabs.PrepareCloseDocumentsAsync(this.window, [.. documents.Select(static document => document.Id)]);
    }

    /// <inheritdoc/>
    public Task<bool> CloseAsync(Document document, bool force = false)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.tabs.CloseDocumentAsync(this.window, document.Id, force);
    }

    /// <summary>
    ///     Stops following the window's tabs. The documents stay open.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.tabs.DocumentOpened -= this.OnTabOpened;
        this.tabs.DocumentAttached -= this.OnTabAttached;
        this.tabs.DocumentActivated -= this.OnTabActivated;
        this.tabs.DocumentDetached -= this.OnTabDetached;
        this.tabs.DocumentClosed -= this.OnTabClosed;
        this.SetActive(document: null);
        foreach (var tab in this.shown)
        {
            tab.Dispose();
        }

        this.shown.Clear();
    }

    /// <summary>
    ///     Gets the tab of a document shown in this area.
    /// </summary>
    /// <param name="document">The document.</param>
    /// <returns>The tab, or <see langword="null"/> when the document has no tab here.</returns>
    internal DocumentTab? TabOf(Document document) => this.Find(document.Id);

    private static DocumentPlacement Cancelled(AssetPath asset) => new() { Asset = asset };

    private static DocumentPlacement Failed(AssetPath asset, string problem, IReadOnlyList<AssetPath>? replaced = null)
        => new() { Asset = asset, Problem = problem, Replaced = replaced ?? [] };

    private async Task<DocumentPlacement> PlaceAsync(AssetPath asset, CancellationToken cancellationToken)
    {
        if (this.files.Find(asset) is not { } file)
        {
            return Failed(asset, "It is not authored in the project's content folders.");
        }

        // A document that is open is shown where it is.
        if (this.documents.Find(file.Path) is { } open)
        {
            return await this.ShowPlacedAsync(asset, open).ConfigureAwait(true);
        }

        if (this.documents.FindKind(file.Path) is not { } kind)
        {
            return Failed(asset, "No editor opens this kind of file.");
        }

        // A newer request for a kind with one document per area supersedes an older one that has not shown its document.
        var onePerArea = this.editors.Of(kind).IsOnePerArea;
        var request = onePerArea ? Interlocked.Increment(ref this.replacement) : 0;
        try
        {
            await this.placements.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return Cancelled(asset);
        }

        try
        {
            // The request before this one may have opened the same file.
            if (this.documents.Find(file.Path) is { } opened)
            {
                return await this.ShowPlacedAsync(asset, opened).ConfigureAwait(true);
            }

            return onePerArea
                ? await this.ReplaceAsync(asset, file, kind, request, cancellationToken).ConfigureAwait(true)
                : await this.OpenNewAsync(asset, file, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return Cancelled(asset);
        }
        finally
        {
            _ = this.placements.Release();
        }
    }

    private async Task<DocumentPlacement> ShowPlacedAsync(AssetPath asset, Document document)
        => await this.ShowAsync(document).ConfigureAwait(true)
            ? new DocumentPlacement { Asset = asset, Document = document }
            : Failed(asset, "Its tab could not be shown.");

    private async Task<DocumentPlacement> OpenNewAsync(AssetPath asset, DocumentFile file, CancellationToken cancellationToken)
    {
        var opened = await this.documents.OpenAsync(file, cancellationToken).ConfigureAwait(true);
        if (opened.Document is not { } document)
        {
            return Failed(asset, opened.Problem ?? "It could not be read.");
        }

        if (await this.ShowAsync(document).ConfigureAwait(true))
        {
            return new DocumentPlacement { Asset = asset, Document = document };
        }

        // A document that never got a tab is closed here; one that has a tab elsewhere stays.
        if (!opened.WasOpen)
        {
            this.documents.Close(document);
        }

        return Failed(asset, "Its tab could not be shown.");
    }

    // The documents it replaces are asked about first, so a cancelled question opens nothing; and the file is read before
    // they close, so a file that does not open keeps them.
    private async Task<DocumentPlacement> ReplaceAsync(AssetPath asset, DocumentFile file, IDocumentKind kind, long request, CancellationToken cancellationToken)
    {
        if (!this.IsCurrent(request))
        {
            return Cancelled(asset);
        }

        var current = this.Documents.Where(document => ReferenceEquals(document.Kind, kind)).ToArray();
        if (current.Length == 0)
        {
            return await this.OpenNewAsync(asset, file, cancellationToken).ConfigureAwait(true);
        }

        var replaced = current.Select(static document => document.File.Asset).ToArray();
        Document incoming;

        // The window takes no new tab while its close is prepared.
        using (var closing = await this.PrepareCloseAsync(current).ConfigureAwait(true))
        {
            if (closing is null || !this.IsCurrent(request))
            {
                return Cancelled(asset);
            }

            cancellationToken.ThrowIfCancellationRequested();
            var opened = await this.documents.OpenAsync(file, cancellationToken).ConfigureAwait(true);
            if (opened.Document is not { } document)
            {
                return Failed(asset, opened.Problem ?? "It could not be read.");
            }

            if (opened.WasOpen)
            {
                return await this.ShowPlacedAsync(asset, document).ConfigureAwait(true);
            }

            if (!this.IsCurrent(request))
            {
                this.documents.Close(document);
                return Cancelled(asset);
            }

            if (!await closing.CommitAsync().ConfigureAwait(true))
            {
                this.documents.Close(document);
                return Failed(asset, "The documents it replaces did not close.");
            }

            incoming = document;
        }

        if (await this.ShowAsync(incoming).ConfigureAwait(true))
        {
            return new DocumentPlacement { Asset = asset, Document = incoming, Replaced = replaced };
        }

        this.documents.Close(incoming);
        return Failed(asset, "Its tab could not be shown.", replaced);
    }

    private bool IsCurrent(long request) => !this.disposed && request == Volatile.Read(ref this.replacement);

    private DocumentTab? Find(DocumentId id) => this.shown.FirstOrDefault(tab => tab.DocumentId == id);

    private async Task<bool> SelectAsync(WindowId owner, Document document)
    {
        if (!await this.tabs.SelectDocumentAsync(owner, document.Id).ConfigureAwait(true))
        {
            this.LogShowRefused(document.Title);
            return false;
        }

        this.bringForward(owner);
        return true;
    }

    private void OnTabOpened(object? sender, DocumentOpenedEventArgs args)
    {
        if (args.WindowId == this.window && args.Metadata is DocumentTab tab && this.shown.Contains(tab))
        {
            this.TabAdded?.Invoke(this, new DocumentTabEventArgs(tab));
            this.DocumentsChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    // A tab another window detached, which a drag dropped here.
    private void OnTabAttached(object? sender, DocumentAttachedEventArgs args)
    {
        if (args.WindowId == this.window && args.Metadata is DocumentTab tab && !this.shown.Contains(tab))
        {
            this.shown.Add(tab);
            this.TabAdded?.Invoke(this, new DocumentTabEventArgs(tab));
            this.DocumentsChanged?.Invoke(this, EventArgs.Empty);
            this.LogTabMoved(tab.Document.Title, entered: true);
        }
    }

    // The tab moves with its document, which stays open.
    private void OnTabDetached(object? sender, DocumentDetachedEventArgs args)
    {
        if (args.WindowId != this.window || args.Metadata is not DocumentTab tab || !this.shown.Remove(tab))
        {
            return;
        }

        if (ReferenceEquals(this.ActiveDocument, tab.Document))
        {
            this.SetActive(document: null);
        }

        this.TabRemoved?.Invoke(this, new DocumentTabEventArgs(tab));
        this.DocumentsChanged?.Invoke(this, EventArgs.Empty);
        this.LogTabMoved(tab.Document.Title, entered: false);
    }

    private void OnTabActivated(object? sender, DocumentActivatedEventArgs args)
    {
        if (args.WindowId == this.window)
        {
            // A tab that is not a document's, such as a legacy editor's, leaves no document active.
            this.SetActive(this.Find(args.DocumentId)?.Document);
        }
    }

    private void OnTabClosed(object? sender, DocumentClosedEventArgs args)
    {
        if (args.WindowId != this.window || args.Metadata is not DocumentTab tab || !this.shown.Remove(tab))
        {
            return;
        }

        if (ReferenceEquals(this.ActiveDocument, tab.Document))
        {
            this.SetActive(document: null);
        }

        this.TabRemoved?.Invoke(this, new DocumentTabEventArgs(tab));
        this.DocumentsChanged?.Invoke(this, EventArgs.Empty);
        tab.Dispose();

        this.LogDocumentClosed(tab.Document.Kind.Name, tab.Document.Title, tab.Document.IsDirty);
        this.documents.Close(tab.Document);
    }

    private void SetActive(Document? document)
    {
        if (!ReferenceEquals(this.ActiveDocument, document))
        {
            this.ActiveDocument = document;
            this.ActiveDocumentChanged?.Invoke(this, EventArgs.Empty);
        }

        var editor = document is null ? null : this.service.EditorOf(document);
        if (!ReferenceEquals(this.ActiveEditor, editor))
        {
            this.ActiveEditor = editor;
            this.ActiveEditorChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private void PublishOpenFailure(DocumentPlacement placement, string problem)
    {
        var unavailable = placement.Replaced.Count != 0;
        var message = unavailable
            ? $"{problem} The document it replaced was closed; open it again or open another one."
            : $"{problem} The open documents were kept.";
        var operationId = Guid.NewGuid();
        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = "Document.Open",
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = $"{placement.Asset.Name} was not opened",
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope { AssetVirtualPath = placement.Asset.VirtualPath },
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.Document,
                    Severity = DiagnosticSeverity.Error,
                    Code = DiagnosticCodes.DocumentPrefix + (unavailable ? "UNAVAILABLE" : "OPEN_FAILED"),
                    Message = message,
                },
            ],
        });
    }
}
