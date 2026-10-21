// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Finishes the pending input of the focused control in a window, before a document operation captures what the
/// document holds.
/// </summary>
public interface IDocumentInputCommitter
{
    /// <summary>Commits the focused control's valid input, or restores its last valid value.</summary>
    /// <param name="window">The window whose focused control commits.</param>
    /// <returns>A task that completes once the control has published its input, on the window's UI thread.</returns>
    public Task CommitAsync(WindowId window);

    /// <summary>
    /// Commits the focused control's valid input in the window that shows a document, or restores its last valid value.
    /// An editor knows its document, not the window that shows it.
    /// </summary>
    /// <param name="document">The document whose window's focused control commits.</param>
    /// <returns>
    /// A task that completes once the control has published its input, at once when no window shows the document.
    /// </returns>
    public Task CommitAsync(DocumentId document);
}
