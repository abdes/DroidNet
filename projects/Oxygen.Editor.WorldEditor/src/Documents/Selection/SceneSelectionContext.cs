// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Documents.Selection;

/// <summary>
/// Classifies the row kinds represented by the current Explorer selection.
/// </summary>
public enum SceneSelectionKind
{
    /// <summary>No rows are selected.</summary>
    Empty,

    /// <summary>The scene root row is selected.</summary>
    Scene,

    /// <summary>Only scene nodes are selected.</summary>
    Node,

    /// <summary>Only explorer folders are selected.</summary>
    Folder,

    /// <summary>A mix of node, folder and/or root rows is selected.</summary>
    Mixed,
}

/// <summary>
/// Immutable, document-scoped snapshot of the Explorer selection, carrying the row
/// kind and explicit primary identity in addition to the selected node/folder ids.
/// </summary>
/// <param name="Kind">The classified selection kind.</param>
/// <param name="SelectedNodeIds">Selected scene-node identities in stable order.</param>
/// <param name="SelectedFolderIds">Selected folder identities in stable order.</param>
/// <param name="PrimaryNodeId">The primary node identity, if a node is the primary.</param>
/// <param name="PrimaryFolderId">The primary folder identity, if a folder is the primary.</param>
public sealed record SceneSelectionContext(
    SceneSelectionKind Kind,
    IReadOnlyList<Guid> SelectedNodeIds,
    IReadOnlyList<Guid> SelectedFolderIds,
    Guid? PrimaryNodeId,
    Guid? PrimaryFolderId)
{
    /// <summary>Gets an empty selection context.</summary>
    public static SceneSelectionContext Empty { get; } = new(SceneSelectionKind.Empty, [], [], null, null);
}
