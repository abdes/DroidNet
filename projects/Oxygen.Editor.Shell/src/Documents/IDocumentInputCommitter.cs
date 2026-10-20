// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// Finishes the pending input of the focused control in a part's window, before a document operation captures what the
/// document holds.
/// </summary>
public interface IDocumentInputCommitter
{
    /// <summary>Commits the focused control's valid input, or restores its last valid value.</summary>
    /// <returns>A task that completes once the control has published its input, on the window's UI thread.</returns>
    public Task CommitAsync();
}
