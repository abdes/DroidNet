// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Documents;

/// <summary>An open document: the file it edits, the version of it it holds, its history and its content.</summary>
/// <remarks>
/// <para>
/// Every document behaves the same way, whatever its kind: it saves against the version it read or last saved, so an
/// outside change surfaces as a conflict instead of being overwritten; it is dirty while its history is away from the
/// position of its last save or load; and it reacts the same way when its file changes. Its kind only decodes the file
/// and captures what to save.
/// </para>
/// <para>
/// A document has no window and no tab. It is used from the thread that edits its content, and its operations run one
/// at a time.
/// </para>
/// </remarks>
public sealed partial class Document : IDisposable
{
    private readonly IAtomicFileStore files;
    private readonly ILogger logger;
    private readonly SemaphoreSlim operations = new(1, 1);
    private HistoryPosition savedPosition;
    private (bool Dirty, bool Missing, bool Changed, DocumentFile File) published;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="Document"/> class.</summary>
    /// <param name="kind">The document's kind.</param>
    /// <param name="file">The file it edits.</param>
    /// <param name="content">The content decoded from the file.</param>
    /// <param name="version">The version of the file the content was decoded from.</param>
    /// <param name="files">The store that reads and writes the file.</param>
    /// <param name="logger">The logger.</param>
    internal Document(IDocumentKind kind, DocumentFile file, IDocumentContent content, FileVersion version, IAtomicFileStore files, ILogger logger)
    {
        this.Kind = kind;
        this.File = file;
        this.Content = content;
        this.Version = version;
        this.files = files;
        this.logger = logger;
        this.History = new HistoryKeeper(this);
        this.savedPosition = this.History.Position;
        this.published = this.CaptureState();
        ((INotifyCollectionChanged)this.History.UndoStack).CollectionChanged += this.OnHistoryChanged;
    }

    /// <summary>Occurs when the document becomes dirty or clean, missing or changed on disk, or follows its file.</summary>
    public event EventHandler? StateChanged;

    /// <summary>Occurs when a reload replaced the content; the history was cleared with it.</summary>
    public event EventHandler<DocumentContentReplacedEventArgs>? ContentReplaced;

    /// <summary>Gets the document's identity, which is also its tab's.</summary>
    public Guid Id { get; } = Guid.NewGuid();

    /// <summary>Gets the document's kind.</summary>
    public IDocumentKind Kind { get; }

    /// <summary>Gets the file the document edits; it changes when the document follows a relocation.</summary>
    public DocumentFile File { get; private set; }

    /// <summary>Gets the document's name: its file name without the suffix of its kind.</summary>
    public string Title => DocumentKinds.TitleOf(this.Kind, this.File.Path);

    /// <summary>Gets the document's history, into which its edits are recorded.</summary>
    /// <remarks>The document never clears it, except when a reload replaces the content the entries refer to.</remarks>
    public HistoryKeeper History { get; }

    /// <summary>Gets the content the document edits.</summary>
    public IDocumentContent Content { get; private set; }

    /// <summary>Gets the version of the file the document read or last saved; a save writes only over that version.</summary>
    public FileVersion Version { get; private set; }

    /// <summary>Gets a value indicating whether the document differs from its file.</summary>
    /// <remarks>
    /// It does while its history is away from the position of the last save or load, or while its file is missing.
    /// Undoing back to the saved position makes it clean again.
    /// </remarks>
    public bool IsDirty => this.HasEdits || this.IsMissingOnDisk;

    /// <summary>Gets a value indicating whether the document's file was deleted or moved outside the editor.</summary>
    /// <remarks>The document stays open; saving writes the file again where it was.</remarks>
    public bool IsMissingOnDisk { get; private set; }

    /// <summary>Gets a value indicating whether the file changed outside the editor while the document had edits.</summary>
    /// <remarks>A save conflicts until the document reloads, keeps its edits over the change, or saves a copy.</remarks>
    public bool IsChangedOnDisk { get; private set; }

    private bool HasEdits => this.History.Position != this.savedPosition;

    /// <summary>Saves the document: finishes its edits in progress, then writes what its content captures.</summary>
    /// <param name="cancellationToken">Cancels the save before the file is replaced.</param>
    /// <returns>Whether the file was written, or why it was not.</returns>
    /// <remarks>
    /// An edit made while the file is written keeps the document dirty. A document without edits writes nothing,
    /// unless its file is missing.
    /// </remarks>
    public async Task<DocumentSaveResult> SaveAsync(CancellationToken cancellationToken = default)
    {
        await this.operations.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            await this.Content.FinishEditsAsync(cancellationToken).ConfigureAwait(true);
            if (!this.IsDirty)
            {
                return new DocumentSaveResult(DocumentSaveOutcome.Saved);
            }

            var capture = this.Content.Capture();
            var position = this.History.Position;
            if (capture.Refusal is { } refusal)
            {
                this.LogSaveRefused(refusal);
                return new DocumentSaveResult(DocumentSaveOutcome.Refused, refusal);
            }

            var bytes = await capture.EncodeAsync(cancellationToken).ConfigureAwait(true);
            var expected = this.IsMissingOnDisk ? FileVersion.Missing : this.Version;
            try
            {
                this.Version = await this.files.WriteAsync(this.File.Path, bytes, expected, cancellationToken).ConfigureAwait(true);
            }
            catch (StorageWriteConflictException ex)
            {
                // The file was replaced, created or deleted since the document held it.
                var current = await this.files.ReadAsync(this.File.Path, cancellationToken).ConfigureAwait(true);
                this.IsMissingOnDisk = !current.Version.Exists;
                this.IsChangedOnDisk = current.Version.Exists;
                this.LogSaveConflict(ex.Message);
                this.PublishState();
                return new DocumentSaveResult(DocumentSaveOutcome.Conflict, ex.Message);
            }

            this.savedPosition = position;
            this.IsMissingOnDisk = false;
            this.IsChangedOnDisk = false;
            this.LogSaved();
            this.PublishState();
            return new DocumentSaveResult(DocumentSaveOutcome.Saved);
        }
        finally
        {
            _ = this.operations.Release();
        }
    }

    /// <summary>Writes the document's content as a new asset; the document itself, and its edits, are unchanged.</summary>
    /// <param name="copy">The new asset's file, which must not exist and must be a file of the document's kind.</param>
    /// <param name="cancellationToken">Cancels the copy before the file is written.</param>
    /// <returns>Whether the copy was written, or why it was not.</returns>
    public async Task<DocumentSaveResult> SaveCopyAsync(DocumentFile copy, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(copy);
        if (DocumentKinds.SuffixOf(this.Kind, copy.Path) is null)
        {
            throw new ArgumentException($"'{copy.Path}' is not a {this.Kind.Name} file.", nameof(copy));
        }

        await this.operations.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            await this.Content.FinishEditsAsync(cancellationToken).ConfigureAwait(true);
            var capture = this.Content.CaptureCopy(copy);
            if (capture.Refusal is { } refusal)
            {
                this.LogSaveRefused(refusal);
                return new DocumentSaveResult(DocumentSaveOutcome.Refused, refusal);
            }

            var bytes = await capture.EncodeAsync(cancellationToken).ConfigureAwait(true);
            try
            {
                _ = await this.files.WriteAsync(copy.Path, bytes, FileVersion.Missing, cancellationToken).ConfigureAwait(true);
            }
            catch (StorageWriteConflictException ex)
            {
                this.LogCopyConflict(copy.Path, ex.Message);
                return new DocumentSaveResult(DocumentSaveOutcome.Conflict, ex.Message);
            }

            this.LogCopySaved(copy.Path);
            return new DocumentSaveResult(DocumentSaveOutcome.Saved);
        }
        finally
        {
            _ = this.operations.Release();
        }
    }

    /// <summary>Reacts to a change of the document's file, which may be its own save heard back.</summary>
    /// <param name="cancellationToken">Cancels reading the file.</param>
    /// <returns>What the document found and did.</returns>
    /// <remarks>
    /// A document without edits reads the changed file again; one with edits, or one being edited, is marked changed on
    /// disk; one whose file is gone is marked missing.
    /// </remarks>
    public async Task<DocumentFileState> CheckFileAsync(CancellationToken cancellationToken = default)
    {
        await this.operations.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            var snapshot = await this.files.ReadAsync(this.File.Path, cancellationToken).ConfigureAwait(true);
            if (!snapshot.Version.Exists)
            {
                return this.MarkMissing();
            }

            if (snapshot.Version == this.Version)
            {
                return this.MarkPresent();
            }

            if (this.HasEdits || this.History.IsBusy)
            {
                return this.MarkChangedOnDisk();
            }

            if (await this.ReplaceContentAsync(snapshot, cancellationToken).ConfigureAwait(true) is { } problem)
            {
                this.LogReloadFailed(problem);
                return this.MarkChangedOnDisk();
            }

            return DocumentFileState.Reloaded;
        }
        finally
        {
            _ = this.operations.Release();
        }
    }

    /// <summary>Reads the file again and replaces the content, discarding the document's edits and its history.</summary>
    /// <param name="cancellationToken">Cancels the reload before the content is replaced.</param>
    /// <returns>Whether the document now holds the file's content, or why it does not.</returns>
    public async Task<DocumentReloadResult> ReloadAsync(CancellationToken cancellationToken = default)
    {
        await this.operations.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            if (this.History.IsBusy)
            {
                return this.ReloadFailed("The document is being edited. Finish the edit, then reload it.");
            }

            var snapshot = await this.files.ReadAsync(this.File.Path, cancellationToken).ConfigureAwait(true);
            if (!snapshot.Version.Exists)
            {
                _ = this.MarkMissing();
                return this.ReloadFailed($"'{this.File.Path}' no longer exists.");
            }

            return await this.ReplaceContentAsync(snapshot, cancellationToken).ConfigureAwait(true) is { } problem
                ? this.ReloadFailed(problem)
                : new DocumentReloadResult(Succeeded: true);
        }
        finally
        {
            _ = this.operations.Release();
        }
    }

    /// <summary>Keeps the document's edits over an outside change: its next save replaces the file as it is now.</summary>
    /// <param name="cancellationToken">Cancels reading the file.</param>
    /// <returns>The task that completes once the document holds the file's current version.</returns>
    public async Task KeepMineAsync(CancellationToken cancellationToken = default)
    {
        await this.operations.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            if (!this.IsChangedOnDisk)
            {
                return;
            }

            var snapshot = await this.files.ReadAsync(this.File.Path, cancellationToken).ConfigureAwait(true);
            this.IsChangedOnDisk = false;
            if (snapshot.Version.Exists)
            {
                this.Version = snapshot.Version;
            }
            else
            {
                this.IsMissingOnDisk = true;
            }

            this.LogKeptMine();
            this.PublishState();
        }
        finally
        {
            _ = this.operations.Release();
        }
    }

    /// <summary>Follows a relocation that moved or rewrote the document's file, keeping its history and its state.</summary>
    /// <param name="relocation">The document's file after the relocation and the assets it moved.</param>
    /// <param name="cancellationToken">Cancels waiting for the document's current operation.</param>
    /// <returns>The task that completes once the document follows its file.</returns>
    /// <exception cref="InvalidOperationException">The document has edits: a relocation only rewrites saved files.</exception>
    public async Task FollowRelocationAsync(DocumentRelocation relocation, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(relocation);
        if (DocumentKinds.SuffixOf(this.Kind, relocation.File.Path) is null)
        {
            throw new ArgumentException($"'{relocation.File.Path}' is not a {this.Kind.Name} file.", nameof(relocation));
        }

        await this.operations.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            if (this.IsDirty || this.IsChangedOnDisk || this.History.IsBusy)
            {
                throw new InvalidOperationException($"'{this.Title}' has unsaved edits; a relocation only rewrites saved documents.");
            }

            var from = this.File.Path;
            this.Content.ApplyRelocation(relocation);
            this.File = relocation.File;
            this.Version = relocation.Version;
            this.LogFollowed(from);
            this.PublishState();
        }
        finally
        {
            _ = this.operations.Release();
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        ((INotifyCollectionChanged)this.History.UndoStack).CollectionChanged -= this.OnHistoryChanged;
        (this.Content as IDisposable)?.Dispose();
        this.operations.Dispose();
    }

    // Returns why the file did not decode, or null once the content is replaced.
    private async Task<string?> ReplaceContentAsync(FileSnapshot snapshot, CancellationToken cancellationToken)
    {
        var decoded = await this.Kind.DecodeAsync(this.File, snapshot.Content.AsMemory(), cancellationToken).ConfigureAwait(true);
        if (decoded.Content is not { } content)
        {
            return decoded.Problem;
        }

        var previous = this.Content;
        this.Content = content;
        this.History.Clear();
        this.savedPosition = this.History.Position;
        this.Version = snapshot.Version;
        this.IsMissingOnDisk = false;
        this.IsChangedOnDisk = false;
        this.LogReloaded();
        this.ContentReplaced?.Invoke(this, new DocumentContentReplacedEventArgs(previous, content));
        (previous as IDisposable)?.Dispose();
        this.PublishState();
        return null;
    }

    private DocumentFileState MarkMissing()
    {
        if (!this.IsMissingOnDisk)
        {
            this.IsMissingOnDisk = true;
            this.IsChangedOnDisk = false;
            this.LogMissing();
            this.PublishState();
        }

        return DocumentFileState.Missing;
    }

    private DocumentFileState MarkPresent()
    {
        if (!this.IsMissingOnDisk && !this.IsChangedOnDisk)
        {
            return DocumentFileState.Unchanged;
        }

        this.IsMissingOnDisk = false;
        this.IsChangedOnDisk = false;
        this.LogRestored();
        this.PublishState();
        return DocumentFileState.Restored;
    }

    private DocumentFileState MarkChangedOnDisk()
    {
        if (!this.IsChangedOnDisk || this.IsMissingOnDisk)
        {
            this.IsChangedOnDisk = true;
            this.IsMissingOnDisk = false;
            this.LogChangedOnDisk();
            this.PublishState();
        }

        return DocumentFileState.ChangedOnDisk;
    }

    private DocumentReloadResult ReloadFailed(string problem)
    {
        this.LogReloadFailed(problem);
        return new DocumentReloadResult(Succeeded: false, problem);
    }

    private void OnHistoryChanged(object? sender, NotifyCollectionChangedEventArgs args) => this.PublishState();

    private (bool Dirty, bool Missing, bool Changed, DocumentFile File) CaptureState()
        => (this.IsDirty, this.IsMissingOnDisk, this.IsChangedOnDisk, this.File);

    private void PublishState()
    {
        var state = this.CaptureState();
        if (state == this.published)
        {
            return;
        }

        this.published = state;
        this.StateChanged?.Invoke(this, EventArgs.Empty);
    }
}
