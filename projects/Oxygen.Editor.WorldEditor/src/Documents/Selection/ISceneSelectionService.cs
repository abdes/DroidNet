// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Selection;

/// <summary>
/// Owns document-scoped scene selection state.
/// </summary>
public interface ISceneSelectionService
{
    /// <summary>
    /// Sets the selected scene-node identities for a document.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="nodes">The selected nodes in stable selection order.</param>
    /// <param name="source">The selection source.</param>
    public void SetSelection(Guid documentId, IReadOnlyList<SceneNode> nodes, string source);

    /// <summary>
    /// Gets selected nodes that still exist in the supplied scene.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="scene">The active scene model.</param>
    /// <returns>Selected nodes that survived in stable order.</returns>
    public IReadOnlyList<SceneNode> GetSelectedNodes(Guid documentId, Scene scene);

    /// <summary>
    /// Reconciles selection against a new or changed scene model.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="scene">The scene model.</param>
    /// <returns>Selected nodes that survived reconciliation.</returns>
    public IReadOnlyList<SceneNode> Reconcile(Guid documentId, Scene scene);

    /// <summary>
    /// Clears selection for a document.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    public void Clear(Guid documentId);
}
