// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Asks the user of the window that closes them about the unsaved documents its close would discard, and keeps the question open while a
/// chosen save fails.
/// </summary>
public interface IDocumentClosePrompt
{
    /// <summary>Asks which unsaved documents to save before they close.</summary>
    /// <param name="window">The window whose documents close, which owns the question.</param>
    /// <param name="documents">The unsaved documents, each chosen for saving at first.</param>
    /// <returns>
    /// <see langword="true"/> once the chosen documents are saved and the others are to be discarded;
    /// <see langword="false"/> when the user cancels.
    /// </returns>
    public Task<bool> ConfirmAsync(WindowId window, IReadOnlyList<DocumentCloseItem> documents);
}
