// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace Oxygen.Editor.Documents;

/// <summary>Finishes the focused control's pending input before a document operation captures authoring data.</summary>
public interface IDocumentInputCommitter
{
    /// <summary>Commits valid focused input or restores its last valid value.</summary>
    /// <param name="windowId">The document's owner window.</param>
    /// <returns>Completion of control input publication on the UI dispatcher.</returns>
    public Task CommitAsync(WindowId windowId);
}
