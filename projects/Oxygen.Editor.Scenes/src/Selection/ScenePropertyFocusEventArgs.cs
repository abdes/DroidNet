// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.Scenes.Selection;

/// <summary>Asks the panel that edits the selected scene's properties to focus one property.</summary>
/// <param name="documentId">The scene document whose property to focus.</param>
/// <param name="propertyPath">The stable property path.</param>
/// <param name="source">The requester.</param>
public sealed class ScenePropertyFocusEventArgs(DocumentId documentId, string propertyPath, string source) : EventArgs
{
    /// <summary>Gets the scene document whose property to focus.</summary>
    public DocumentId DocumentId { get; } = documentId;

    /// <summary>Gets the stable property path.</summary>
    public string PropertyPath { get; } = propertyPath;

    /// <summary>Gets the requester.</summary>
    public string Source { get; } = source;
}
