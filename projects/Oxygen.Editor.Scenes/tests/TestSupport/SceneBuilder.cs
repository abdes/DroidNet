// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// Builds the <see cref="SceneData"/> of a test scene: nodes in declaration order, their components, and an explorer
/// layout that holds every node and folder exactly where the test declared it.
/// </summary>
/// <remarks>
/// A node's layout entry goes in its folder when one is given, otherwise under its parent node, otherwise at the root.
/// Entries keep declaration order within their container. Every <see cref="Build"/> returns a new, independent copy, so one builder can serve several scenes.
/// </remarks>
public sealed class SceneBuilder
{
    private readonly List<NodeSpec> nodes = [];
    private readonly List<FolderSpec> folders = [];
    private int order;
    private SceneId id = SceneId.New();
    private string name = "Scene";
    private SceneEnvironmentData environment = new();
    private NodeId? startingCamera;
    private IList<StartingInputContextData>? startingInputContexts;
    private SceneReferencesData? references;

    /// <summary>
    /// Gets the id the builder gives a node's transform, so that every build of a scene has the same ids.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <returns>The transform's id.</returns>
    public static ComponentId TransformId(NodeId node)
    {
        var bytes = node.Value.ToByteArray();
        bytes[8] ^= 0xFF;
        return ComponentId.FromValue(new Guid(bytes));
    }

    /// <summary>
    /// Adds a node. It always gets a transform, <paramref name="transform"/> or identity, whose id is
    /// <see cref="TransformId"/>.
    /// </summary>
    /// <param name="nodeId">The node's id.</param>
    /// <param name="nodeName">The node's name.</param>
    /// <param name="parent">The parent node, already added; <see langword="null"/> for a root node.</param>
    /// <param name="folder">The folder that holds the node's layout entry, already added, in the parent's scope.</param>
    /// <param name="transform">The node's transform; identity when <see langword="null"/>.</param>
    /// <param name="components">The node's other components. Give them fixed ids when tests name them.</param>
    /// <returns>This builder.</returns>
    /// <exception cref="ArgumentException">An id is taken, or the parent or the folder is not there or not in scope.</exception>
    public SceneBuilder Node(
        NodeId nodeId,
        string nodeName,
        NodeId? parent = null,
        FolderId? folder = null,
        TransformData? transform = null,
        IEnumerable<ComponentData>? components = null)
    {
        if (this.nodes.Exists(node => node.Id == nodeId))
        {
            throw new ArgumentException($"Node {nodeId} is already added.", nameof(nodeId));
        }

        if (parent is { } parentId && this.nodes.TrueForAll(node => node.Id != parentId))
        {
            throw new ArgumentException($"Parent {parentId} must be added before '{nodeName}'.", nameof(parent));
        }

        if (folder is { } folderId && this.FindFolder(folderId) is var spec && (spec is null || spec.Scope != parent))
        {
            throw new ArgumentException(
                spec is null ? $"Folder {folderId} must be added before '{nodeName}'." : $"Folder '{spec.Name}' is not in the scope of the parent of '{nodeName}'.",
                nameof(folder));
        }

        ComponentData[] all = [(transform ?? new TransformData { Name = "Transform" }) with { Id = TransformId(nodeId) }, .. components ?? []];
        var taken = this.nodes.SelectMany(static node => node.Components).Select(static component => component.Id).ToHashSet();
        if (!all.All(component => taken.Add(component.Id)))
        {
            throw new ArgumentException($"A component id of '{nodeName}' is already taken.", nameof(components));
        }

        this.nodes.Add(new NodeSpec(this.order++, nodeId, nodeName, parent, folder, all));
        return this;
    }

    /// <summary>
    /// Adds an explorer folder, in a node's layout scope or inside another folder.
    /// </summary>
    /// <param name="folderId">The folder's id.</param>
    /// <param name="folderName">The folder's name.</param>
    /// <param name="underNode">The node whose layout scope holds the folder; <see langword="null"/> for the root scope.</param>
    /// <param name="inFolder">The folder that holds this folder, already added; the folder then shares its scope.</param>
    /// <param name="isExpanded">Whether the folder is expanded.</param>
    /// <returns>This builder.</returns>
    /// <exception cref="ArgumentException">The id is taken, both places are given, or the place is not there.</exception>
    public SceneBuilder Folder(FolderId folderId, string folderName, NodeId? underNode = null, FolderId? inFolder = null, bool isExpanded = true)
    {
        if (this.FindFolder(folderId) is not null)
        {
            throw new ArgumentException($"Folder {folderId} is already added.", nameof(folderId));
        }

        if (underNode is not null && inFolder is not null)
        {
            throw new ArgumentException($"Folder '{folderName}' is either under a node or in a folder, not both.", nameof(inFolder));
        }

        if (underNode is { } nodeId && this.nodes.TrueForAll(node => node.Id != nodeId))
        {
            throw new ArgumentException($"Node {nodeId} must be added before folder '{folderName}'.", nameof(underNode));
        }

        var scope = underNode;
        if (inFolder is { } outer)
        {
            scope = (this.FindFolder(outer) ?? throw new ArgumentException($"Folder {outer} must be added before folder '{folderName}'.", nameof(inFolder))).Scope;
        }

        this.folders.Add(new FolderSpec(this.order++, folderId, folderName, scope, inFolder, isExpanded));
        return this;
    }

    /// <summary>
    /// Removes a node, its descendants, and the folders in their layout scopes.
    /// </summary>
    /// <param name="nodeId">The node to remove.</param>
    /// <returns>This builder.</returns>
    /// <exception cref="ArgumentException">The node is not in the scene.</exception>
    public SceneBuilder Without(NodeId nodeId)
    {
        if (this.nodes.TrueForAll(node => node.Id != nodeId))
        {
            throw new ArgumentException($"Node {nodeId} is not in the scene.", nameof(nodeId));
        }

        // Parents come before their children, so one pass in declaration order finds every descendant.
        var removed = new HashSet<NodeId> { nodeId };
        foreach (var node in this.nodes.Where(node => node.Parent is { } parent && removed.Contains(parent)))
        {
            _ = removed.Add(node.Id);
        }

        // A folder's scope is its owner node's, so the folders of removed scopes hold only removed nodes and folders.
        _ = this.nodes.RemoveAll(node => removed.Contains(node.Id));
        _ = this.folders.RemoveAll(folder => folder.Scope is { } owner && removed.Contains(owner));
        return this;
    }

    /// <summary>
    /// Sets the scene's id; a new id is used otherwise.
    /// </summary>
    /// <param name="sceneId">The scene id.</param>
    /// <returns>This builder.</returns>
    public SceneBuilder WithId(SceneId sceneId)
    {
        this.id = sceneId;
        return this;
    }

    /// <summary>
    /// Sets the scene's name. A decoded scene takes its name from its file instead.
    /// </summary>
    /// <param name="sceneName">The scene name.</param>
    /// <returns>This builder.</returns>
    public SceneBuilder Named(string sceneName)
    {
        this.name = sceneName;
        return this;
    }

    /// <summary>
    /// Sets the scene's environment.
    /// </summary>
    /// <param name="value">The environment.</param>
    /// <returns>This builder.</returns>
    public SceneBuilder Environment(SceneEnvironmentData value)
    {
        this.environment = value;
        return this;
    }

    /// <summary>
    /// Sets the camera node the scene starts viewing through.
    /// </summary>
    /// <param name="value">The camera node.</param>
    /// <returns>This builder.</returns>
    public SceneBuilder StartingCamera(NodeId value)
    {
        this.startingCamera = value;
        return this;
    }

    /// <summary>
    /// Sets the input mapping contexts that are active when the scene starts.
    /// </summary>
    /// <param name="value">The contexts, in order.</param>
    /// <returns>This builder.</returns>
    public SceneBuilder StartingInputContexts(IList<StartingInputContextData> value)
    {
        this.startingInputContexts = value;
        return this;
    }

    /// <summary>
    /// Sets the scene's references.
    /// </summary>
    /// <param name="value">The references.</param>
    /// <returns>This builder.</returns>
    public SceneBuilder References(SceneReferencesData value)
    {
        this.references = value;
        return this;
    }

    /// <summary>
    /// Builds a new, independent copy of the scene data.
    /// </summary>
    /// <returns>The scene data.</returns>
    public SceneFileData Build()
    {
        var data = new SceneFileData
        {
            Id = this.id,
            Name = this.name,
            RootNodes = [.. this.NodesUnder(parent: null)],
            Environment = this.environment,
            StartingCamera = this.startingCamera,
            StartingInputContexts = this.startingInputContexts,
            References = this.references,
            ExplorerLayout = [.. this.EntriesIn(underNode: null, inFolder: null)],
        };

        // A serializer round trip gives every build its own lists and proves the data is a valid scene file.
        return JsonSerializer.Deserialize(JsonSerializer.SerializeToUtf8Bytes(data, SceneFileJsonContext.Default.SceneFileData), SceneFileJsonContext.Default.SceneFileData)!;
    }

    private static ExplorerEntryData NodeEntry(NodeId nodeId, List<ExplorerEntryData> children)
        => new() { Type = "Node", NodeId = nodeId, Children = children.Count == 0 ? null : children };

    private FolderSpec? FindFolder(FolderId folderId) => this.folders.Find(folder => folder.Id == folderId);

    private IEnumerable<SceneNodeData> NodesUnder(NodeId? parent)
        => this.nodes.Where(node => node.Parent == parent).Select(node =>
        {
            var children = this.NodesUnder(node.Id).ToList();
            return new SceneNodeData
            {
                Id = node.Id,
                Name = node.Name,
                Components = [.. node.Components],
                Children = children.Count == 0 ? null : children,
                IsActive = true,
            };
        });

    private IEnumerable<ExplorerEntryData> EntriesIn(NodeId? underNode, FolderId? inFolder)
    {
        var folderEntries = this.folders
            .Where(folder => inFolder is { } outer ? folder.InFolder == outer : folder.InFolder is null && folder.Scope == underNode)
            .Select(folder => (folder.Order, Entry: new ExplorerEntryData
            {
                Type = "Folder",
                FolderId = folder.Id,
                Name = folder.Name,
                IsExpanded = folder.IsExpanded,
                Children = [.. this.EntriesIn(underNode: null, inFolder: folder.Id)],
            }));
        var nodeEntries = this.nodes
            .Where(node => inFolder is { } outer ? node.Folder == outer : node.Folder is null && node.Parent == underNode)
            .Select(node => (node.Order, Entry: NodeEntry(node.Id, [.. this.EntriesIn(node.Id, inFolder: null)])));

        // Nodes and folders keep the order in which the test declared them.
        return nodeEntries.Concat(folderEntries).OrderBy(static pair => pair.Order).Select(static pair => pair.Entry);
    }

    private sealed record NodeSpec(int Order, NodeId Id, string Name, NodeId? Parent, FolderId? Folder, IReadOnlyList<ComponentData> Components);

    private sealed record FolderSpec(int Order, FolderId Id, string Name, NodeId? Scope, FolderId? InFolder, bool IsExpanded);
}
