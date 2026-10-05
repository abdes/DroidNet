// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message indicating that the document selection has changed. The node list is
/// the selected-node subset (viewport and demand consumers); the typed
/// <see cref="SelectionContext"/> carries the full row-kind classification,
/// ordered identities and explicit primary so consumers never have to infer
/// "scene" or "folder" from the shape of the node list.
/// </summary>
/// <param name="selectedEntities">The list of selected <see cref="SceneNode"/> entities.</param>
/// <param name="selectionContext">
///     The classified selection context, or <see langword="null" /> from node-only
///     publishers, which derives a plain node (or empty) classification.
/// </param>
/// <param name="documentId">The owning document, including for selections with no nodes.</param>
internal sealed class SceneNodeSelectionChangedMessage(
    IList<SceneNode> selectedEntities,
    SceneSelectionContext? selectionContext = null,
    Guid? documentId = null)
{
    /// <summary>Gets the owning document identity when supplied by the publisher.</summary>
    public Guid? DocumentId { get; } = documentId;

    /// <summary>
    /// Gets the list of selected <see cref="SceneNode"/> entities.
    /// </summary>
    public IList<SceneNode> SelectedEntities { get; } = selectedEntities;

    /// <summary>Gets the full classified context of the selection.</summary>
    public SceneSelectionContext SelectionContext { get; } = selectionContext ?? DeriveNodeContext(selectedEntities);

    private static SceneSelectionContext DeriveNodeContext(IList<SceneNode> nodes)
    {
        var orderedIds = nodes
            .Where(node => node is not null)
            .Select(node => node.Id)
            .Distinct()
            .ToArray();

        return orderedIds.Length == 0
            ? SceneSelectionContext.Empty
            : new SceneSelectionContext(SceneSelectionKind.Node, orderedIds, [], orderedIds[^1], null);
    }
}
