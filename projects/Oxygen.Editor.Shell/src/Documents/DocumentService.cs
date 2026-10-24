// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The document service of a project session: it creates the editor of each document that gets a tab in any of the
///     session's windows, makes the close decisions of every window, and keeps the documents in step with their files.
/// </summary>
/// <remarks>
///     Every operation runs on the UI thread, where the documents live. An editor is created when its document's tab
///     opens and disposed when the document closes, after its window has let go of it; a tab that moves to another
///     window keeps its editor. Changes the catalog reports for a document's file make the document check it: a clean
///     document reloads, an edited one is marked changed on disk, and a deleted one missing. A relocation in the editor
///     moves or rewrites the documents it touches, and closes the documents whose files it deleted.
/// </remarks>
public sealed partial class DocumentService : IDocumentService, IDisposable
{
    private readonly IDocumentTabs tabs;
    private readonly OpenDocuments documents;
    private readonly IDocumentFiles files;
    private readonly IDocumentConfirmations confirmations;
    private readonly IDocumentInputCommitter inputCommitter;
    private readonly IDocumentRelocations? relocations;
    private readonly HostingContext hosting;
    private readonly ILogger logger;
    private readonly DocumentCloseCoordinator closeCoordinator;
    private readonly IDisposable? catalogChanges;
    private readonly IDisposable? relocationFollower;
    private readonly Dictionary<DocumentId, (DocumentTab Tab, object Editor)> editors = [];
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentService"/> class.
    /// </summary>
    /// <param name="tabs">The tabs of the session's windows, whose documents get their editors here.</param>
    /// <param name="closeCoordinator">The session's close decisions, which ask the editors created here.</param>
    /// <param name="documents">The open documents of the session.</param>
    /// <param name="files">Where the project keeps the files that documents edit.</param>
    /// <param name="confirmations">Asks a window's user to save documents before an operation that reads saved files.</param>
    /// <param name="inputCommitter">Commits the focused control's input before a document operation.</param>
    /// <param name="hosting">The UI thread, where the documents live.</param>
    /// <param name="catalog">The catalog whose changes name the files that changed on disk, if any.</param>
    /// <param name="relocations">The relocations of the project's assets, if any.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public DocumentService(
        IDocumentTabs tabs,
        DocumentCloseCoordinator closeCoordinator,
        OpenDocuments documents,
        IDocumentFiles files,
        IDocumentConfirmations confirmations,
        IDocumentInputCommitter inputCommitter,
        HostingContext hosting,
        IAssetCatalog? catalog = null,
        IDocumentRelocations? relocations = null,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(tabs);
        ArgumentNullException.ThrowIfNull(closeCoordinator);
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(files);
        ArgumentNullException.ThrowIfNull(confirmations);
        ArgumentNullException.ThrowIfNull(inputCommitter);
        ArgumentNullException.ThrowIfNull(hosting);
        this.tabs = tabs;
        this.documents = documents;
        this.files = files;
        this.confirmations = confirmations;
        this.inputCommitter = inputCommitter;
        this.relocations = relocations;
        this.hosting = hosting;
        this.logger = loggerFactory?.CreateLogger<DocumentService>() ?? NullLogger<DocumentService>.Instance;
        this.closeCoordinator = closeCoordinator;

        this.tabs.DocumentOpened += this.OnTabOpened;
        this.documents.Closed += this.OnDocumentClosed;
        this.catalogChanges = catalog?.Changes.Subscribe(change => this.OnUiThread(() => this.OnSourceChanged(change)));
        if (relocations is not null)
        {
            relocations.FilesDeleted += this.OnFilesDeleted;
            this.relocationFollower = relocations.Follow(this.FollowRelocationAsync);
        }
    }

    /// <summary>
    ///     Raised on the UI thread after the editor of a document's tab is created, before the tab is selected.
    /// </summary>
    internal event EventHandler<DocumentTabEventArgs>? EditorCreated;

    /// <inheritdoc/>
    public object? EditorOf(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        return this.editors.TryGetValue(document.Id, out var entry) ? entry.Editor : null;
    }

    /// <inheritdoc/>
    public async Task<bool> SaveAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        if (this.EditorOf(document) is IAsyncSaveable editor)
        {
            // The editor saves its own document, so its input commit and its prompts run.
            await editor.SaveAsync().ConfigureAwait(true);
        }
        else
        {
            await this.inputCommitter.CommitAsync(document.Id).ConfigureAwait(true);
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
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A recovery failure is shown beside the document, which stays open with its changes.")]
    public async Task<DocumentConflictResult> ReloadAsync(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        try
        {
            await this.inputCommitter.CommitAsync(document.Id).ConfigureAwait(true);
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
            await this.inputCommitter.CommitAsync(document.Id).ConfigureAwait(true);
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
    ///     Stops following the tabs and the files, and disposes the editors. The documents stay open.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.tabs.DocumentOpened -= this.OnTabOpened;
        this.documents.Closed -= this.OnDocumentClosed;
        this.catalogChanges?.Dispose();
        this.relocationFollower?.Dispose();
        this.relocations?.FilesDeleted -= this.OnFilesDeleted;

        foreach (var (_, editor) in this.editors.Values)
        {
            (editor as IDisposable)?.Dispose();
        }

        this.editors.Clear();
    }

    // A tab of one of the session's documents gets its editor, in whichever window it opens.
    private void OnTabOpened(object? sender, DocumentOpenedEventArgs args)
    {
        if (this.disposed || args.Metadata is not DocumentTab tab || !this.documents.Documents.Contains(tab.Document)
            || this.editors.ContainsKey(tab.DocumentId))
        {
            return;
        }

        var editor = tab.Editor.CreateEditor(tab);
        this.editors[tab.DocumentId] = (tab, editor);
        if (editor is IDocumentCloseParticipant participant)
        {
            this.closeCoordinator.Register(tab, participant, this);
        }
        else
        {
            this.LogEditorNotClosable(tab.Title);
        }

        this.EditorCreated?.Invoke(this, new DocumentTabEventArgs(tab));
    }

    // The window closes a document after its tab is gone and it has let go of the editor.
    private void OnDocumentClosed(object? sender, DocumentEventArgs args)
    {
        if (!this.editors.Remove(args.Document.Id, out var entry))
        {
            return;
        }

        this.closeCoordinator.Unregister(entry.Tab);
        (entry.Editor as IDisposable)?.Dispose();
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
        foreach (var document in this.documents.Documents.Where(document => document.File.Asset == identity || document.File.Asset == previous).ToArray())
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

    private Task FollowRelocationAsync(Func<Document, DocumentRelocation?> map)
        => this.hosting.Dispatcher.HasThreadAccess
            ? this.FollowOnThreadAsync(map)
            : this.hosting.Dispatcher.DispatchAsync(() => this.FollowOnThreadAsync(map));

    private async Task FollowOnThreadAsync(Func<Document, DocumentRelocation?> map)
    {
        foreach (var document in this.documents.Documents.ToArray())
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

    // Deleting an open asset in the editor closes its document in the window that shows it; the delete already asked.
    private async Task CloseDeletedAsync(IReadOnlyList<FileSystemPath> deleted)
    {
        foreach (var document in this.documents.Documents.ToArray())
        {
            var path = document.File.Path;
            if (this.disposed || !deleted.Any(path.IsSameOrInside) || this.tabs.FindWindow(document.Id) is not { } window)
            {
                continue;
            }

            if (await this.tabs.CloseDocumentAsync(window, document.Id, force: true).ConfigureAwait(true))
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
        if (this.hosting.Dispatcher.HasThreadAccess)
        {
            action();
        }
        else
        {
            _ = this.hosting.Dispatcher.TryEnqueue(() => action());
        }
    }
}
