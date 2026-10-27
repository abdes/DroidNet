// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Selection;

/// <summary>
/// The editor's single owner of scene selection: one stored <see cref="SceneSelectionContext"/> per document, which
/// every panel reads and writes, and a change event for each write.
/// </summary>
public sealed class SceneSelectionService : ISceneSelectionService
{
    // One stored context per document: kind, ordered node references and folder ids, and the explicit primary.
    // The writer's source travels with the change event and is deliberately not stored.
    private readonly Dictionary<DocumentId, SceneSelectionContext> contexts = [];

    /// <inheritdoc />
    public event EventHandler<SceneSelectionChangedEventArgs>? SelectionChanged;

    /// <inheritdoc />
    public event EventHandler<ScenePropertyFocusEventArgs>? PropertyFocusRequested;

    /// <inheritdoc />
    public void SetSelection(DocumentId documentId, IReadOnlyList<NodeReference> nodes, string source)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        this.PublishCore(documentId, SceneSelectionContext.ForNodes(nodes.Where(static node => node is not null)), source);
    }

    /// <inheritdoc />
    public IReadOnlyList<SceneNode> GetSelectedNodes(DocumentId documentId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        if (!this.contexts.TryGetValue(documentId, out var context))
        {
            return [];
        }

        var byId = scene.AllNodes.ToDictionary(node => node.Id);
        return [.. context.OwnedNodeIds
            .Where(byId.ContainsKey)
            .Select(id => byId[id])];
    }

    /// <inheritdoc />
    public IReadOnlyList<SceneNode> Reconcile(DocumentId documentId, Scene scene, IList<ExplorerEntryData> layout)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layout);

        if (!this.contexts.TryGetValue(documentId, out var context))
        {
            return [];
        }

        var byId = scene.AllNodes.ToDictionary(node => node.Id);
        var survivingNodes = context.SelectedNodes.Where(scene.Holds).ToArray();

        var folderIds = GetFolderIds(layout);
        var survivingFolderIds = context.SelectedFolderIds.Where(folderIds.Contains).ToArray();
        var primaryNode = context.PrimaryNode is { } primary && survivingNodes.Contains(primary)
            ? primary
            : null;
        FolderId? primaryFolder = context.PrimaryFolderId is { } folderId && survivingFolderIds.Contains(folderId)
            ? folderId
            : null;
        // Without a surviving primary, the first surviving node leads, else the first surviving folder.
        if (primaryNode is null && primaryFolder is null)
        {
            if (survivingNodes.Length > 0)
            {
                primaryNode = survivingNodes[0];
            }
            else if (survivingFolderIds.Length > 0)
            {
                primaryFolder = survivingFolderIds[0];
            }
        }

        var kind = SceneSelectionContext.Classify(
            context.Kind == SceneSelectionKind.Scene,
            survivingFolderIds.Length > 0,
            survivingNodes.Length > 0);

        if (kind == SceneSelectionKind.Empty)
        {
            this.Clear(documentId);
            return [];
        }

        IReadOnlyList<SceneNode> survivingOwned = [.. survivingNodes.Where(static node => node.IsOwned).Select(node => byId[node.Node])];
        if (kind == context.Kind
            && survivingNodes.SequenceEqual(context.SelectedNodes)
            && survivingFolderIds.SequenceEqual(context.SelectedFolderIds)
            && primaryNode == context.PrimaryNode
            && primaryFolder == context.PrimaryFolderId)
        {
            return survivingOwned;
        }

        var updated = new SceneSelectionContext(
            kind,
            survivingNodes,
            survivingFolderIds,
            primaryNode,
            primaryFolder);
        this.PublishCore(documentId, updated, "Reconcile");
        return survivingOwned;
    }

    /// <inheritdoc />
    public void Clear(DocumentId documentId)
    {
        if (!this.contexts.Remove(documentId))
        {
            return;
        }

        this.SelectionChanged?.Invoke(
            this,
            new SceneSelectionChangedEventArgs(documentId, SceneSelectionContext.Empty, "Clear"));
    }

    /// <inheritdoc />
    public void Publish(DocumentId documentId, SceneSelectionContext context, string source)
    {
        ArgumentNullException.ThrowIfNull(context);
        this.PublishCore(documentId, context, source);
    }

    /// <inheritdoc />
    public SceneSelectionContext GetContext(DocumentId documentId)
        => this.contexts.TryGetValue(documentId, out var context) ? context : SceneSelectionContext.Empty;

    /// <inheritdoc />
    public void RequestPropertyFocus(DocumentId documentId, string propertyPath, string source)
    {
        ArgumentException.ThrowIfNullOrEmpty(propertyPath);
        ArgumentException.ThrowIfNullOrEmpty(source);
        this.PropertyFocusRequested?.Invoke(this, new ScenePropertyFocusEventArgs(documentId, propertyPath, source));
    }

    private static HashSet<FolderId> GetFolderIds(IList<ExplorerEntryData> layout)
    {
        var ids = new HashSet<FolderId>();
        var pending = new Stack<ExplorerEntryData>(layout);
        while (pending.TryPop(out var entry))
        {
            if (string.Equals(entry.Type, "Folder", StringComparison.OrdinalIgnoreCase) && entry.FolderId is { } id)
            {
                _ = ids.Add(id);
            }

            if (entry.Children is { } children)
            {
                foreach (var child in children)
                {
                    pending.Push(child);
                }
            }
        }

        return ids;
    }

    private void PublishCore(DocumentId documentId, SceneSelectionContext context, string source)
    {
        this.contexts[documentId] = context;
        this.SelectionChanged?.Invoke(
            this,
            new SceneSelectionChangedEventArgs(documentId, context, source));
    }
}
