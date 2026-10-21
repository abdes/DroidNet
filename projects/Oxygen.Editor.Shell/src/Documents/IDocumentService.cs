// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// The editor's one document service: it opens a file once, places its document in a window's document area, saves,
/// reloads and closes documents of every kind the same way, and follows their files when they change, move or vanish.
/// Each document's editor comes from its kind's <see cref="DocumentEditorDescriptor"/>.
/// </summary>
public interface IDocumentService
{
    /// <summary>Raised after every request to open a document completes, whether it succeeded or not.</summary>
    public event EventHandler<DocumentPlacedEventArgs>? Placed;

    /// <summary>Raised on the UI thread after <see cref="ActiveEditor"/> changes.</summary>
    public event EventHandler? ActiveEditorChanged;

    /// <summary>Gets the editor of the active document, or <see langword="null"/> when no document is active.</summary>
    public object? ActiveEditor { get; }

    /// <summary>Gets the editor of a document shown in this service's area.</summary>
    /// <param name="document">The document.</param>
    /// <returns>The editor, or <see langword="null"/> when the document has no tab here.</returns>
    public object? EditorOf(Document document);

    /// <summary>
    /// Opens an asset's document and shows it. A document that is open is shown where it is; a kind that allows one
    /// document per area first closes the one it replaces, asking about its unsaved changes.
    /// </summary>
    /// <param name="asset">The asset to edit.</param>
    /// <param name="cancellationToken">Cancels the open before the document is shown.</param>
    /// <returns>Where the document was placed, or why it was not.</returns>
    public Task<DocumentPlacement> OpenAsync(AssetPath asset, CancellationToken cancellationToken = default);

    /// <summary>Saves a document through its editor, so that the editor's pending input is committed first.</summary>
    /// <param name="document">An open document.</param>
    /// <returns><see langword="true"/> when the document is clean afterwards.</returns>
    public Task<bool> SaveAsync(Document document);

    /// <summary>Saves the open documents of some assets; an asset without an open document is skipped.</summary>
    /// <param name="assets">The assets.</param>
    /// <returns><see langword="true"/> when every one of those documents is clean afterwards.</returns>
    public Task<bool> SaveAsync(IReadOnlyList<AssetPath> assets);

    /// <summary>Saves every unsaved document shown in this service's area.</summary>
    /// <returns><see langword="true"/> when every document is clean afterwards.</returns>
    public Task<bool> SaveAllAsync();

    /// <summary>
    /// Saves the unsaved documents an operation that reads saved files needs, after asking: edits in progress are
    /// finished first, then <paramref name="findUnsaved"/> names the documents, and the user confirms saving them.
    /// </summary>
    /// <param name="operation">The operation, which names the confirmation.</param>
    /// <param name="findUnsaved">Finds the unsaved documents the operation reads.</param>
    /// <param name="cancellationToken">Cancels finding the documents.</param>
    /// <returns>
    /// <see langword="true"/> when the operation can run: nothing it reads was unsaved, or every document was saved;
    /// <see langword="false"/> when the user cancelled or a save failed, which its document shows.
    /// </returns>
    public Task<bool> SaveBeforeAsync(
        SaveBeforeOperation operation,
        Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>> findUnsaved,
        CancellationToken cancellationToken = default);

    /// <summary>Discards a document's changes and its history, and reads its file again.</summary>
    /// <param name="document">An open document.</param>
    /// <returns>The outcome, with the message to show beside the document.</returns>
    public Task<DocumentConflictResult> ReloadAsync(Document document);

    /// <summary>Keeps a document's changes over a file that changed on disk; the next save replaces the file.</summary>
    /// <param name="document">An open document.</param>
    /// <returns>A task that completes once the document holds the file's current version.</returns>
    public Task KeepMineAsync(Document document);

    /// <summary>
    /// Saves a document's content as a new file beside its own, with its own asset identity. The document stays unsaved.
    /// </summary>
    /// <param name="document">An open document.</param>
    /// <param name="name">The copy's name, without its suffix.</param>
    /// <returns>The outcome, with the message to show beside the document.</returns>
    public Task<DocumentConflictResult> SaveCopyAsync(Document document, string name);
}
