// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// The project session's document service: it owns the editor of each open document, saves, reloads and keeps documents
/// of every kind the same way, and follows their files when they change, move or vanish. Each document's editor comes
/// from its kind's <see cref="DocumentEditorDescriptor"/>; each window's <see cref="IDocumentArea"/> opens and shows
/// documents.
/// </summary>
public interface IDocumentService
{
    /// <summary>Gets the editor of an open document of the session.</summary>
    /// <param name="document">The document.</param>
    /// <returns>The editor, or <see langword="null"/> when the document has no tab.</returns>
    public object? EditorOf(Document document);

    /// <summary>Saves a document through its editor, so that the editor's pending input is committed first.</summary>
    /// <param name="document">An open document.</param>
    /// <returns><see langword="true"/> when the document is clean afterwards.</returns>
    public Task<bool> SaveAsync(Document document);

    /// <summary>Saves the open documents of some assets; an asset without an open document is skipped.</summary>
    /// <param name="assets">The assets.</param>
    /// <returns><see langword="true"/> when every one of those documents is clean afterwards.</returns>
    public Task<bool> SaveAsync(IReadOnlyList<AssetPath> assets);

    /// <summary>
    /// Saves the unsaved documents an operation that reads saved files needs, after asking: edits in progress are
    /// finished first, then <paramref name="findUnsaved"/> names the documents, and the user confirms saving them.
    /// </summary>
    /// <param name="window">The window the operation was asked in, which commits its input and asks the user.</param>
    /// <param name="operation">The operation, which names the confirmation.</param>
    /// <param name="findUnsaved">Finds the unsaved documents the operation reads.</param>
    /// <param name="cancellationToken">Cancels finding the documents.</param>
    /// <returns>
    /// <see langword="true"/> when the operation can run: nothing it reads was unsaved, or every document was saved;
    /// <see langword="false"/> when the user cancelled or a save failed, which its document shows.
    /// </returns>
    public Task<bool> SaveBeforeAsync(
        WindowId window,
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
