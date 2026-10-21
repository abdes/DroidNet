// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Selection;

/// <summary>
/// The editor's single owner of scene selection: one stored <see cref="SceneSelectionContext"/> per document, which
/// every panel reads and writes, and a change event for each write.
/// </summary>
public sealed class SceneSelectionService : ISceneSelectionService
{
    // One stored context per document: kind, ordered node/folder ids and the explicit primary.
    // The writer's source travels with the change event and is deliberately not stored.
    private readonly Dictionary<DocumentId, SceneSelectionContext> contexts = [];

    /// <inheritdoc />
    public event EventHandler<SceneSelectionChangedEventArgs>? SelectionChanged;

    /// <inheritdoc />
    public event EventHandler<ScenePropertyFocusEventArgs>? PropertyFocusRequested;

    /// <inheritdoc />
    public void SetSelection(DocumentId documentId, IReadOnlyList<SceneNode> nodes, string source)
    {
        ArgumentNullException.ThrowIfNull(nodes);

        var orderedIds = nodes
            .Where(node => node is not null)
            .Select(node => node.Id)
            .Distinct()
            .ToArray();

        var context = orderedIds.Length == 0
            ? SceneSelectionContext.Empty
            : new SceneSelectionContext(
                SceneSelectionKind.Node,
                orderedIds,
                [],
                orderedIds[^1],
                PrimaryFolderId: null);

        this.PublishCore(documentId, context, source);
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
        return [.. context.SelectedNodeIds
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
        var survivingIds = context.SelectedNodeIds
            .Where(byId.ContainsKey)
            .ToArray();

        var folderIds = GetFolderIds(layout);
        var survivingFolderIds = context.SelectedFolderIds.Where(folderIds.Contains).ToArray();
        NodeId? primaryNode = context.PrimaryNodeId is { } nodeId && survivingIds.Contains(nodeId)
            ? nodeId
            : null;
        FolderId? primaryFolder = context.PrimaryFolderId is { } folderId && survivingFolderIds.Contains(folderId)
            ? folderId
            : null;
        if (primaryNode is null && primaryFolder is null)
        {
            primaryNode = survivingIds.Length > 0 ? survivingIds[0] : null;
            primaryFolder = primaryNode is null && survivingFolderIds.Length > 0 ? survivingFolderIds[0] : null;
        }

        var kind = SceneSelectionContext.Classify(
            context.Kind == SceneSelectionKind.Scene,
            survivingFolderIds.Length > 0,
            survivingIds.Length > 0);

        if (kind == SceneSelectionKind.Empty)
        {
            this.Clear(documentId);
            return [];
        }

        if (kind == context.Kind
            && survivingIds.SequenceEqual(context.SelectedNodeIds)
            && survivingFolderIds.SequenceEqual(context.SelectedFolderIds)
            && primaryNode == context.PrimaryNodeId
            && primaryFolder == context.PrimaryFolderId)
        {
            return [.. survivingIds.Select(id => byId[id])];
        }

        var updated = new SceneSelectionContext(
            kind,
            survivingIds,
            survivingFolderIds,
            primaryNode,
            primaryFolder);
        this.PublishCore(documentId, updated, "Reconcile");
        return [.. survivingIds.Select(id => byId[id])];
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
