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
}
