// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace Oxygen.Editor.Documents;

/// <summary>Presents unsaved documents and keeps the decision open when a selected save fails.</summary>
public interface IDocumentClosePrompt
{
    /// <summary>Requests permission to close dirty documents.</summary>
    /// <param name="windowId">The owner window.</param>
    /// <param name="documents">Dirty documents, initially all selected for saving.</param>
    /// <param name="isWorkspaceClose">Whether to present the combined workspace dialog.</param>
    /// <returns>True after successful selected saves or an explicit discard; false on cancel.</returns>
    public Task<bool> ConfirmAsync(WindowId windowId, IReadOnlyList<DocumentCloseItem> documents, bool isWorkspaceClose);
}
