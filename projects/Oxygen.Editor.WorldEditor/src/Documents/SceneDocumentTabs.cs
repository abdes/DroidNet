// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>Finds scene documents among a window's tabs, which the document service keys by its own ids.</summary>
internal static class SceneDocumentTabs
{
    /// <summary>Gets the scene document of the window's active tab.</summary>
    /// <param name="documents">The window's document service.</param>
    /// <param name="windowId">The window.</param>
    /// <returns>The active scene document, or <see langword="null"/> when the active tab is not a scene.</returns>
    public static DocumentId? Active(IDocumentService documents, WindowId windowId)
    {
        ArgumentNullException.ThrowIfNull(documents);
        return documents.GetActiveDocumentId(windowId) is { } tab
            && documents.GetOpenDocuments(windowId).FirstOrDefault(metadata => metadata.DocumentId == tab) is SceneDocumentMetadata { Document: { } document }
                ? document.Id
                : null;
    }
}
