// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// The tabs of every window, which Aura draws: at most one tab per document, and the all-or-nothing close of a set of
/// tabs. Only the Shell uses it; features reach their window's documents through <see cref="IDocumentArea"/>.
/// </summary>
public interface IDocumentTabs : IDocumentTabSource
{
    /// <summary>Prepares all documents before the owning workspace is torn down.</summary>
    /// <param name="windowId">The workspace window.</param>
    /// <returns>A transaction to commit after other close guards approve, or null when vetoed.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAllAsync(WindowId windowId);

    /// <summary>Prepares all workspace windows before the host replaces the active project.</summary>
    /// <returns>A transaction retaining all old documents until committed, or null when any guard vetoes replacement.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAllWindowsAsync();

    /// <summary>Prepares closing some tabs of a window without closing them until the caller commits.</summary>
    /// <param name="windowId">The window that has the tabs.</param>
    /// <param name="documentIds">The documents whose tabs close.</param>
    /// <returns>The guarded close transaction, or null when a document has no tab in the window or the close is vetoed.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentsAsync(WindowId windowId, IReadOnlyList<DocumentId> documentIds);

    /// <summary>Finds the window that has a document's tab.</summary>
    /// <param name="documentId">The document.</param>
    /// <returns>The window, or <see langword="null"/> when the document has no tab.</returns>
    public WindowId? FindWindow(DocumentId documentId);

    /// <summary>
    /// Has a window's close decisions made by its close coordinator: which documents are saved, and which editors
    /// veto. Without one, a window refuses to close a dirty document unless forced.
    /// </summary>
    /// <param name="windowId">The window.</param>
    /// <param name="coordinator">The window's close coordinator.</param>
    /// <returns>Stops using the coordinator when disposed.</returns>
    public IDisposable UseCloseCoordinator(WindowId windowId, DocumentCloseCoordinator coordinator);
}
