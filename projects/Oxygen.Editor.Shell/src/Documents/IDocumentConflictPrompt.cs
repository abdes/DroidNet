// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;

namespace Oxygen.Editor.Documents;

/// <summary>Presents conflict actions after an ordinary Save, outside a close-dialog callback.</summary>
public interface IDocumentConflictPrompt
{
    /// <summary>Shows conflict actions and waits for any accepted action before returning.</summary>
    /// <param name="windowId">The document's owner window.</param>
    /// <param name="metadata">The original document.</param>
    /// <param name="participant">Its conflict-resolution owner.</param>
    /// <returns>The prompt task.</returns>
    public Task ShowAsync(WindowId windowId, IDocumentMetadata metadata, IDocumentConflictParticipant participant);
}
