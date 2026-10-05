// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;

namespace Oxygen.Editor.Documents;

/// <summary>Extends document lifecycle with an all-or-cancel workspace close preparation.</summary>
public interface IEditorDocumentService : IDocumentService
{
    /// <summary>Prepares all documents before the owning workspace is torn down.</summary>
    /// <param name="windowId">The workspace window.</param>
    /// <returns>A transaction to commit after other close guards approve, or null when vetoed.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAllAsync(WindowId windowId);

    /// <summary>Prepares all workspace windows before the host replaces the active project.</summary>
    /// <returns>A transaction retaining all old documents until committed, or null when any guard vetoes replacement.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAllWindowsAsync();

    /// <summary>Prepares a document for replacement without retiring it until the caller commits.</summary>
    /// <param name="windowId">The owning workspace.</param>
    /// <param name="documentId">The document being replaced.</param>
    /// <returns>The guarded close transaction, or null when unavailable or vetoed.</returns>
    public Task<DocumentCloseTransaction?> PrepareCloseDocumentAsync(WindowId windowId, Guid documentId);
}
