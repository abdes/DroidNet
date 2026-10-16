// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Names the selection a command left in its document, after it added or removed nodes.</summary>
/// <param name="selectedNodes">The selected nodes.</param>
/// <param name="selectionContext">The document's selection, with its row kinds.</param>
/// <param name="documentId">The document's identity.</param>
public sealed class SceneCommandSelectionEventArgs(IReadOnlyList<SceneNode> selectedNodes, SceneSelectionContext selectionContext, DocumentId documentId) : EventArgs
{
    /// <summary>Gets the selected nodes.</summary>
    public IReadOnlyList<SceneNode> SelectedNodes { get; } = selectedNodes;

    /// <summary>Gets the document's selection, with its row kinds.</summary>
    public SceneSelectionContext SelectionContext { get; } = selectionContext;

    /// <summary>Gets the document's identity.</summary>
    public DocumentId DocumentId { get; } = documentId;
}
