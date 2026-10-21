// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The document service of one window's document area: it creates the editor of each document that gets a tab
///     there, places the documents it opens, and keeps them in step with their files.
/// </summary>
/// <remarks>
///     Every operation runs on the window's UI thread, where its documents live. Changes the catalog reports for a
///     document's file make the document check it: a clean document reloads, an edited one is marked changed on disk, and
///     a deleted one missing. A relocation in the editor moves or rewrites the documents it touches, and closes the
///     documents whose files it deleted.
/// </remarks>
public sealed partial class DocumentService : IDocumentService, IDisposable
{
    private readonly DocumentArea area;
    private readonly OpenDocuments documents;
    private readonly IDocumentFiles files;
    private readonly IDocumentConfirmations confirmations;
    private readonly IDocumentInputCommitter inputCommitter;
    private readonly IOperationResultPublisher operationResults;
    private readonly IDocumentRelocations? relocations;
    private readonly ILogger logger;
    private readonly SynchronizationContext? uiThread;
    private readonly DocumentCloseCoordinator closeCoordinator;
    private readonly IDisposable closeCoordinatorUse;
    private readonly IDisposable? catalogChanges;
    private readonly IDisposable? relocationFollower;
    private readonly Dictionary<DocumentId, object> editors = [];

    [SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "An open in flight releases the gate after Dispose; the gate holds no wait handle.")]
    private readonly SemaphoreSlim placements = new(1, 1);

    private long replacement;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentService"/> class.
    /// </summary>
    /// <param name="area">The document area the service places documents in.</param>
    /// <param name="tabs">The tabs of every window; the service makes the close decisions of its area's window.</param>
    /// <param name="documents">The open documents of the session.</param>
    /// <param name="files">Where the project keeps the files that documents edit.</param>
    /// <param name="closePrompt">Asks the window's user about unsaved documents that close.</param>
    /// <param name="confirmations">Asks the window's user to save documents before an operation that reads saved files.</param>
    /// <param name="inputCommitter">Commits the focused control's input before a document operation.</param>
    /// <param name="operationResults">Publishes the failures of opens and closes.</param>
    /// <param name="catalog">The catalog whose changes name the files that changed on disk, if any.</param>
    /// <param name="relocations">The relocations of the project's assets, if any.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public DocumentService(
        DocumentArea area,
        IDocumentTabs tabs,
        OpenDocuments documents,
        IDocumentFiles files,
        IDocumentClosePrompt closePrompt,
        IDocumentConfirmations confirmations,
        IDocumentInputCommitter inputCommitter,
        IOperationResultPublisher operationResults,
        IAssetCatalog? catalog = null,
        IDocumentRelocations? relocations = null,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(area);
        ArgumentNullException.ThrowIfNull(tabs);
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(closePrompt);
        ArgumentNullException.ThrowIfNull(confirmations);
        ArgumentNullException.ThrowIfNull(inputCommitter);
        ArgumentNullException.ThrowIfNull(operationResults);
        this.area = area;
        this.documents = documents;
        this.files = files;
        this.confirmations = confirmations;
        this.inputCommitter = inputCommitter;
        this.operationResults = operationResults;
        this.relocations = relocations;
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<DocumentService>();
        this.uiThread = SynchronizationContext.Current;
        this.closeCoordinator = new DocumentCloseCoordinator(closePrompt, operationResults, this);
        this.closeCoordinatorUse = tabs.UseCloseCoordinator(area.Window, this.closeCoordinator);

        this.area.TabAdded += this.OnTabAdded;
        this.area.TabRemoved += this.OnTabRemoved;
        this.area.ActiveDocumentChanged += this.OnActiveDocumentChanged;
        this.catalogChanges = catalog?.Changes.Subscribe(change => this.OnUiThread(() => this.OnSourceChanged(change)));
        if (relocations is not null)
        {
            relocations.FilesDeleted += this.OnFilesDeleted;
            this.relocationFollower = relocations.Follow(this.FollowRelocationAsync);
        }
    }

    /// <inheritdoc/>
    public event EventHandler<DocumentPlacedEventArgs>? Placed;

    /// <inheritdoc/>
    public event EventHandler? ActiveEditorChanged;

    /// <inheritdoc/>
    public object? ActiveEditor { get; private set; }

    // Without a UI thread (tests), work runs where it is raised.
    private bool IsOnUiThread => this.uiThread is null || SynchronizationContext.Current == this.uiThread;

    /// <inheritdoc/>
    public object? EditorOf(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.editors.GetValueOrDefault(document.Id);
    }

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
    public async Task<bool> SaveAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        if (this.editors.GetValueOrDefault(document.Id) is IAsyncSaveable editor)
        {
            // The editor saves its own document, so its input commit and its prompts run.
            await editor.SaveAsync().ConfigureAwait(true);
        }
        else
        {
            await this.inputCommitter.CommitAsync().ConfigureAwait(true);
            _ = await document.SaveAsync().ConfigureAwait(true);
        }

        return !document.IsDirty;
    }

    /// <inheritdoc/>
    public async Task<bool> SaveAsync(IReadOnlyList<AssetPath> assets)
    {
        ArgumentNullException.ThrowIfNull(assets);
        foreach (var document in assets.Select(this.documents.FindByAsset).OfType<Document>().ToArray())
        {
            if (!await this.SaveAsync(document).ConfigureAwait(true))
            {
                return false;
            }
        }

        return true;
    }

    /// <inheritdoc/>
    public async Task<bool> SaveAllAsync()
    {
        foreach (var document in this.area.Documents.Where(static document => document.IsDirty))
        {
            if (!await this.SaveAsync(document).ConfigureAwait(true))
            {
                return false;
            }
        }

        return true;
    }

    /// <inheritdoc/>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A recovery failure is shown beside the document, which stays open with its changes.")]
    public async Task<DocumentConflictResult> ReloadAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        try
        {
            await this.inputCommitter.CommitAsync().ConfigureAwait(true);
            var result = await document.ReloadAsync().ConfigureAwait(true);
            return result.Succeeded
                ? new(Succeeded: true, "Reloaded from disk. Unsaved changes and Undo/Redo history were discarded.")
                : new(Succeeded: false, (result.Problem ?? "The document could not be reloaded.") + " Your changes are still open.");
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return new(Succeeded: false, $"Reload failed. Your changes are still open. {exception.Message}");
        }
    }

    /// <inheritdoc/>
    public Task KeepMineAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return document.KeepMineAsync();
    }

    /// <inheritdoc/>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A recovery failure is shown beside the document, which stays open with its changes.")]
    public async Task<DocumentConflictResult> SaveCopyAsync(Document document, string name)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(name);
        var typed = VirtualPath.NormalizeTypedName(name);
        var file = typed + DocumentKinds.SuffixOf(document.Kind, document.File.Path);
        if (VirtualPath.ValidateSegment(file, VirtualPathRuleSet.Authored) is { } invalid)
        {
            return new(Succeeded: false, invalid.ToMessage() + " Your original document is still open.");
        }

        if (document.File.Path.Parent is not { } folder || this.files.Find(folder.Combine(file)) is not { } copy)
        {
            return new(Succeeded: false, "The copy would be outside the project's content folders. Your original document is still open.");
        }

        try
        {
            await this.inputCommitter.CommitAsync().ConfigureAwait(true);
            var saved = await document.SaveCopyAsync(copy).ConfigureAwait(true);
            var message = saved.Succeeded
                ? $"Saved “{typed}” as a separate asset. The original is still unsaved; keep it open or explicitly discard its changes."
                : saved.Outcome == DocumentSaveOutcome.Conflict
                    ? $"A file named “{typed}” already exists. Choose another name."
                    : $"The copy was not saved. {saved.Message}";
            return new(Succeeded: saved.Succeeded, message);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return new(Succeeded: false, $"The copy was not saved. Your original document is still open. {exception.Message}");
        }
    }

    /// <summary>
    ///     Stops following the area and the files, and disposes the editors. The documents stay open.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.area.TabAdded -= this.OnTabAdded;
        this.area.TabRemoved -= this.OnTabRemoved;
        this.area.ActiveDocumentChanged -= this.OnActiveDocumentChanged;
        this.catalogChanges?.Dispose();
        this.relocationFollower?.Dispose();
        this.relocations?.FilesDeleted -= this.OnFilesDeleted;

        this.closeCoordinatorUse.Dispose();
        this.SetActiveEditor(editor: null);
        foreach (var editor in this.editors.Values)
        {
            (editor as IDisposable)?.Dispose();
        }

        this.editors.Clear();
    }

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
            return await this.ShowAsync(asset, open).ConfigureAwait(true);
        }

        if (this.documents.FindKind(file.Path) is not { } kind)
        {
            return Failed(asset, "No editor opens this kind of file.");
        }

        // A newer request for a kind with one document per area supersedes an older one that has not shown its document.
        var onePerArea = this.area.EditorOf(kind).IsOnePerArea;
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
                return await this.ShowAsync(asset, opened).ConfigureAwait(true);
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

    private async Task<DocumentPlacement> ShowAsync(AssetPath asset, Document document)
        => await this.area.ShowAsync(document).ConfigureAwait(true)
            ? new DocumentPlacement { Asset = asset, Document = document }
            : Failed(asset, "Its tab could not be shown.");

    private async Task<DocumentPlacement> OpenNewAsync(AssetPath asset, DocumentFile file, CancellationToken cancellationToken)
    {
        var opened = await this.documents.OpenAsync(file, cancellationToken).ConfigureAwait(true);
        if (opened.Document is not { } document)
        {
            return Failed(asset, opened.Problem ?? "It could not be read.");
        }

        if (await this.area.ShowAsync(document).ConfigureAwait(true))
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

        var current = this.area.Documents.Where(document => ReferenceEquals(document.Kind, kind)).ToArray();
        if (current.Length == 0)
        {
            return await this.OpenNewAsync(asset, file, cancellationToken).ConfigureAwait(true);
        }

        var replaced = current.Select(static document => document.File.Asset).ToArray();
        Document incoming;

        // The window takes no new tab while its close is prepared.
        using (var closing = await this.area.PrepareCloseAsync(current).ConfigureAwait(true))
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
                return await this.ShowAsync(asset, document).ConfigureAwait(true);
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

        if (await this.area.ShowAsync(incoming).ConfigureAwait(true))
        {
            return new DocumentPlacement { Asset = asset, Document = incoming, Replaced = replaced };
        }

        this.documents.Close(incoming);
        return Failed(asset, "Its tab could not be shown.", replaced);
    }

    private bool IsCurrent(long request) => !this.disposed && request == Volatile.Read(ref this.replacement);

    private void OnTabAdded(object? sender, DocumentTabEventArgs args)
    {
        var tab = args.Tab;
        var editor = tab.Editor.CreateEditor(tab);
        this.editors[tab.DocumentId] = editor;
        if (editor is IDocumentCloseParticipant participant)
        {
            this.closeCoordinator.Register(tab, participant);
        }
        else
        {
            this.LogEditorNotClosable(tab.Title);
        }
    }

    private void OnTabRemoved(object? sender, DocumentTabEventArgs args)
    {
        var tab = args.Tab;
        this.closeCoordinator.Unregister(tab);
        if (!this.editors.Remove(tab.DocumentId, out var editor))
        {
            return;
        }

        if (ReferenceEquals(this.ActiveEditor, editor))
        {
            this.SetActiveEditor(editor: null);
        }

        (editor as IDisposable)?.Dispose();
    }

    private void OnActiveDocumentChanged(object? sender, EventArgs args)
        => this.SetActiveEditor(this.area.ActiveDocument is { } document ? this.editors.GetValueOrDefault(document.Id) : null);

    private void SetActiveEditor(object? editor)
    {
        if (ReferenceEquals(this.ActiveEditor, editor))
        {
            return;
        }

        this.ActiveEditor = editor;
        this.ActiveEditorChanged?.Invoke(this, EventArgs.Empty);
    }

    // Catalog changes name the source file; a document is the asset that file stands for.
    private void OnSourceChanged(AssetChange change)
    {
        if (this.disposed)
        {
            return;
        }

        var identity = AssetFileTypes.ToIdentity(change.Identity);
        var previous = change.PreviousIdentity is { } moved ? AssetFileTypes.ToIdentity(moved) : null;
        foreach (var document in this.area.Documents.Where(document => document.File.Asset == identity || document.File.Asset == previous))
        {
            _ = this.CheckFileAsync(document);
        }
    }

    private async Task CheckFileAsync(Document document)
    {
        try
        {
            _ = await document.CheckFileAsync().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ObjectDisposedException)
        {
            // The file is unreadable right now, or the document closed; the next change checks again.
            this.LogFileCheckFailed(document.Title, exception.Message);
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The failure is forwarded to the relocation, which awaits it.")]
    private Task FollowRelocationAsync(Func<Document, DocumentRelocation?> map)
    {
        if (this.IsOnUiThread)
        {
            return this.FollowOnThreadAsync(map);
        }

        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        this.uiThread!.Post(_ => _ = FollowAndCompleteAsync(), state: null);
        return completion.Task;

        async Task FollowAndCompleteAsync()
        {
            try
            {
                await this.FollowOnThreadAsync(map).ConfigureAwait(true);
                completion.SetResult();
            }
            catch (Exception failure)
            {
                completion.SetException(failure);
            }
        }
    }

    private async Task FollowOnThreadAsync(Func<Document, DocumentRelocation?> map)
    {
        foreach (var document in this.area.Documents)
        {
            if (!this.disposed && map(document) is { } relocation)
            {
                var from = document.File.Path;
                await document.FollowRelocationAsync(relocation).ConfigureAwait(true);
                this.LogFollowedRelocation(document.Title, from);
            }
        }
    }

    private void OnFilesDeleted(object? sender, DocumentFilesDeletedEventArgs args)
        => this.OnUiThread(() => _ = this.CloseDeletedAsync(args.Files));

    // Deleting an open asset in the editor closes its document; the delete already asked about it.
    private async Task CloseDeletedAsync(IReadOnlyList<FileSystemPath> deleted)
    {
        foreach (var document in this.area.Documents)
        {
            var path = document.File.Path;
            if (this.disposed || !deleted.Any(path.IsSameOrInside))
            {
                continue;
            }

            if (await this.area.CloseAsync(document, force: true).ConfigureAwait(true))
            {
                this.LogDeletedDocumentClosed(document.Title, path);
            }
            else
            {
                this.LogDeletedDocumentNotClosed(document.Title, path);
            }
        }
    }

    private void OnUiThread(Action action)
    {
        if (this.IsOnUiThread)
        {
            action();
        }
        else
        {
            this.uiThread!.Post(_ => action(), state: null);
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
