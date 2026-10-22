// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The document area of the window a part runs in: the documents that have a tab there, which one is active and its
///     editor, and the documents this window opens.
/// </summary>
/// <remarks>
///     A part gets the area of its own window from its scope and never names the window. A document's tab is in at most
///     one area. The area closes a document in <see cref="OpenDocuments"/> once its tab has closed; editors never do. The
///     documents and their editors belong to the project session; the area only shows them.
/// </remarks>
public interface IDocumentArea
{
    /// <summary>
    ///     Raised on the UI thread after <see cref="ActiveDocument"/> changes.
    /// </summary>
    public event EventHandler? ActiveDocumentChanged;

    /// <summary>
    ///     Raised on the UI thread after <see cref="ActiveEditor"/> changes.
    /// </summary>
    public event EventHandler? ActiveEditorChanged;

    /// <summary>
    ///     Raised after every request of this window to open a document completes, whether it succeeded or not.
    /// </summary>
    public event EventHandler<DocumentPlacedEventArgs>? Placed;

    /// <summary>
    ///     Raised on the UI thread after <see cref="Documents"/> changes: a tab was added to this area, closed, or moved
    ///     to another window.
    /// </summary>
    public event EventHandler? DocumentsChanged;

    /// <summary>
    ///     Gets the documents that have a tab in this area.
    /// </summary>
    public IReadOnlyList<Document> Documents { get; }

    /// <summary>
    ///     Gets the document whose tab is selected, or <see langword="null"/> when no document's tab is.
    /// </summary>
    public Document? ActiveDocument { get; }

    /// <summary>
    ///     Gets the editor of the active document, or <see langword="null"/> when no document is active.
    /// </summary>
    public object? ActiveEditor { get; }

    /// <summary>
    ///     Opens an asset's document and shows it. A document that is open is shown where it is; a kind that allows one
    ///     document per area first closes the one it replaces in this area, asking about its unsaved changes.
    /// </summary>
    /// <param name="asset">The asset to edit.</param>
    /// <param name="cancellationToken">Cancels the open before the document is shown.</param>
    /// <returns>Where the document was placed, or why it was not.</returns>
    public Task<DocumentPlacement> OpenAsync(AssetPath asset, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Selects the document's tab and brings its window forward, adding the tab here first when the document has
    ///     none. A document whose tab is in another window is shown there.
    /// </summary>
    /// <param name="document">An open document.</param>
    /// <returns>Whether the document's tab is selected.</returns>
    public Task<bool> ShowAsync(Document document);

    /// <summary>
    ///     Saves every unsaved document that has a tab in this area.
    /// </summary>
    /// <returns><see langword="true"/> when every one of them is clean afterwards.</returns>
    public Task<bool> SaveAllAsync();

    /// <summary>
    ///     Saves the unsaved documents an operation that reads saved files needs, after asking in this window: edits in
    ///     progress are finished first, then <paramref name="findUnsaved"/> names the documents, and the user confirms
    ///     saving them.
    /// </summary>
    /// <param name="operation">The operation, which names the confirmation.</param>
    /// <param name="findUnsaved">Finds the unsaved documents the operation reads.</param>
    /// <param name="cancellationToken">Cancels finding the documents.</param>
    /// <returns>
    ///     <see langword="true"/> when the operation can run: nothing it reads was unsaved, or every document was saved;
    ///     <see langword="false"/> when the user cancelled or a save failed, which its document shows.
    /// </returns>
    public Task<bool> SaveBeforeAsync(
        SaveBeforeOperation operation,
        Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>> findUnsaved,
        CancellationToken cancellationToken = default);

    /// <summary>
    ///     Prepares closing documents' tabs: asks about their unsaved edits and lets their editors veto, without closing
    ///     yet.
    /// </summary>
    /// <param name="documents">Documents with a tab in this area.</param>
    /// <returns>
    ///     The transaction to commit, or <see langword="null"/> when the close was refused or a document has no tab here.
    /// </returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAsync(IReadOnlyList<Document> documents);

    /// <summary>
    ///     Closes a document's tab, then the document.
    /// </summary>
    /// <param name="document">A document with a tab in this area.</param>
    /// <param name="force">Whether to close without asking about unsaved edits and without vetoes.</param>
    /// <returns>Whether the document closed.</returns>
    public Task<bool> CloseAsync(Document document, bool force = false);
}
