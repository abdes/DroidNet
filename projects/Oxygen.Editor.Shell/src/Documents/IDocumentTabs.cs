// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// The tabs of a project session's windows, which Aura draws: at most one tab per document, and the all-or-nothing close
/// of a set of tabs. Only the Shell and the session's owner use it; features reach their window's documents through
/// <see cref="IDocumentArea"/>.
/// </summary>
public interface IDocumentTabs : IDocumentTabSource
{
    /// <summary>
    ///     Raised when the last tab of a window has left it: its own close, such as the user's, closed it, or a drag moved it
    ///     to another window. Closing a set of a window's tabs, or all of them as the window's own close and its session's
    ///     do, does not raise it.
    /// </summary>
    public event EventHandler<WindowEmptiedEventArgs>? WindowEmptied;

    /// <summary>Prepares all documents before the owning workspace is torn down.</summary>
    /// <param name="windowId">The workspace window.</param>
    /// <returns>A transaction to commit after other close guards approve, or null when vetoed.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAllAsync(WindowId windowId);

    /// <summary>
    ///     Prepares closing the documents of every window of the session, such as before its project closes, without
    ///     closing them until the caller commits.
    /// </summary>
    /// <param name="except">A window whose documents are left out, because its own close prepares them.</param>
    /// <returns>A transaction retaining all the documents until committed, or null when any guard vetoes the close.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAllWindowsAsync(WindowId? except = null);

    /// <summary>Prepares closing some tabs of a window without closing them until the caller commits.</summary>
    /// <param name="windowId">The window that has the tabs.</param>
    /// <param name="documentIds">The documents whose tabs close.</param>
    /// <returns>The guarded close transaction, or null when a document has no tab in the window or the close is vetoed.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentsAsync(WindowId windowId, IReadOnlyList<DocumentId> documentIds);

    /// <summary>Finds the window that has a document's tab.</summary>
    /// <param name="documentId">The document.</param>
    /// <returns>The window, or <see langword="null"/> when the document has no tab.</returns>
    public WindowId? FindWindow(DocumentId documentId);

}
