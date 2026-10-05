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
    /// <summary>Gets the selected node identities captured independently of the caller's collection.</summary>
    public IReadOnlyList<Guid> SelectedNodeIds { get; } = Array.AsReadOnly(SelectedNodeIds.ToArray());

    /// <summary>Gets the selected folder identities captured independently of the caller's collection.</summary>
    public IReadOnlyList<Guid> SelectedFolderIds { get; } = Array.AsReadOnly(SelectedFolderIds.ToArray());

    /// <summary>Gets an empty selection context.</summary>
    public static SceneSelectionContext Empty { get; } = new(SceneSelectionKind.Empty, [], [], null, null);

    /// <summary>
    /// Classifies a selection from the presence of each row kind, so every
    /// producer labels a node/folder/root/mixed batch the same way.
    /// </summary>
    /// <param name="hasScene">Whether the scene root row is part of the selection.</param>
    /// <param name="hasFolder">Whether one or more explorer folders are selected.</param>
    /// <param name="hasNode">Whether one or more scene nodes are selected.</param>
    /// <returns>The selection kind for the combination.</returns>
    public static SceneSelectionKind Classify(bool hasScene, bool hasFolder, bool hasNode) =>
        (hasScene, hasFolder, hasNode) switch
        {
            (true, false, false) => SceneSelectionKind.Scene,
            (false, false, true) => SceneSelectionKind.Node,
            (false, true, false) => SceneSelectionKind.Folder,
            (false, false, false) => SceneSelectionKind.Empty,
            _ => SceneSelectionKind.Mixed,
        };
}
