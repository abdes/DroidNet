// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Selection;

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
    /// Raised when a part asks for a property of a scene to be focused, such as the cook issue that names it. The panel
    /// that edits the scene's properties focuses it once the scene is the one it shows.
    /// </summary>
    public event EventHandler<ScenePropertyFocusEventArgs>? PropertyFocusRequested;

    /// <summary>
    /// Sets the selected scene nodes for a document, classified as a
    /// node selection with the last node as primary.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="nodes">
    /// The selected nodes in stable selection order: nodes of the scene and nodes an instance brought in.
    /// </param>
    /// <param name="source">The selection source.</param>
    public void SetSelection(DocumentId documentId, IReadOnlyList<NodeReference> nodes, string source);

    /// <summary>
    /// Gets the selected nodes of the scene itself that still exist in the supplied scene.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="scene">The active scene model.</param>
    /// <returns>
    /// Selected nodes that survived in stable order; a selected node an instance brought in has no scene node and is
    /// not among them.
    /// </returns>
    public IReadOnlyList<SceneNode> GetSelectedNodes(DocumentId documentId, Scene scene);

    /// <summary>
    /// Reconciles selection against a new or changed scene model.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="scene">The scene model.</param>
    /// <param name="layout">The scene's explorer layout, whose folders a folder selection must still name.</param>
    /// <returns>
    /// Selected nodes of the scene itself that survived reconciliation. A selected node an instance brought in
    /// survives while its instance root is in the scene.
    /// </returns>
    public IReadOnlyList<SceneNode> Reconcile(DocumentId documentId, Scene scene, IList<ExplorerEntryData> layout);

    /// <summary>
    /// Clears selection for a document.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    public void Clear(DocumentId documentId);

    /// <summary>
    /// Publishes the full selection context (kind, ordered identities and explicit
    /// primary) as the document's selection state in one write.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <param name="context">The classified selection context.</param>
    /// <param name="source">The selection source.</param>
    public void Publish(DocumentId documentId, SceneSelectionContext context, string source);

    /// <summary>
    /// Gets the last selection context for a document.
    /// </summary>
    /// <param name="documentId">The document identity.</param>
    /// <returns>The selection context, or <see cref="SceneSelectionContext.Empty"/> when none.</returns>
    public SceneSelectionContext GetContext(DocumentId documentId);

    /// <summary>Asks for a property of a scene document to be focused, once the scene's properties are shown.</summary>
    /// <param name="documentId">The scene document.</param>
    /// <param name="propertyPath">The stable property path.</param>
    /// <param name="source">The requester.</param>
    public void RequestPropertyFocus(DocumentId documentId, string propertyPath, string source);
}
