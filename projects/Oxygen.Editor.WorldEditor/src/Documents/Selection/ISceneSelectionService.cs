// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Selection;

/// <summary>
/// Owns document-scoped scene selection state. This is the single selection
/// authority: writers publish a classified <see cref="SceneSelectionContext"/>
/// (row kinds, ordered identities, explicit primary) and subscribers observe
/// every change through <see cref="SelectionChanged"/>.
/// </summary>
public interface ISceneSelectionService
{
    /// <summary>
    /// Raised after any selection write for any document, carrying the document
    /// identity, the stored context and the writer's source tag. The Explorer
    /// consumes this to reconcile its rows with selections made by other panels
    /// without echoing its own writes.
    /// </summary>
    public event EventHandler<SceneSelectionChangedEventArgs>? SelectionChanged;

    /// <summary>
    /// Sets the selected scene-node identities for a document, classified as a
    /// node selection with the last node as primary.
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

    /// <summary>
    /// Publishes the full selection context (kind, ordered identities and explicit
    /// primary) as the document's selection state in one write.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="context">The classified selection context.</param>
    /// <param name="source">The selection source.</param>
    public void Publish(Guid documentId, SceneSelectionContext context, string source);

    /// <summary>
    /// Gets the last selection context for a document.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <returns>The selection context, or <see cref="SceneSelectionContext.Empty"/> when none.</returns>
    public SceneSelectionContext GetContext(Guid documentId);
}
