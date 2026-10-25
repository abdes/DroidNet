// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// What an instance of a source contains, read from the engine's composed report of the source's cooked scene: every
/// node it brings in by node path, nested instances included.
/// </summary>
public sealed class SceneStructure
{
    private readonly ImmutableDictionary<NodePath, StructureNode> byPath;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneStructure"/> class.
    /// </summary>
    /// <param name="source">The source the structure is read for.</param>
    /// <param name="scene">The source's cooked scene.</param>
    /// <param name="nodes">The nodes, in the report's order.</param>
    /// <param name="unresolved">The changes of the composition that resolve to nothing.</param>
    /// <param name="issues">The problems of the composition.</param>
    /// <exception cref="InvalidDataException">Two nodes have one path.</exception>
    internal SceneStructure(AssetPath source, StructureScene scene, ImmutableArray<StructureNode> nodes, ImmutableArray<StructureUnresolvedChange> unresolved, ImmutableArray<StructureIssue> issues)
    {
        var paths = ImmutableDictionary.CreateBuilder<NodePath, StructureNode>();
        foreach (var node in nodes)
        {
            if (!paths.TryAdd(node.Path, node))
            {
                throw new InvalidDataException($"The composed scene names node path '{node.Path}' twice.");
            }
        }

        this.Source = source;
        this.Scene = scene;
        this.Nodes = nodes;
        this.byPath = paths.ToImmutable();
        this.Unresolved = unresolved;
        this.Issues = issues;
    }

    /// <summary>Gets the source the structure is read for: a model or an authored scene.</summary>
    public AssetPath Source { get; }

    /// <summary>Gets the source's cooked scene.</summary>
    public StructureScene Scene { get; }

    /// <summary>Gets every node, the source's own first, then each instance's, depth first.</summary>
    public ImmutableArray<StructureNode> Nodes { get; }

    /// <summary>Gets the changes that resolve to nothing in the instances they are made on.</summary>
    public ImmutableArray<StructureUnresolvedChange> Unresolved { get; }

    /// <summary>Gets the problems the engine met composing the source.</summary>
    public ImmutableArray<StructureIssue> Issues { get; }

    /// <summary>Finds a node by its path below the instance root.</summary>
    /// <param name="path">The node path.</param>
    /// <returns>The node, or null when the source brings in no such node.</returns>
    public StructureNode? Find(NodePath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return this.byPath.GetValueOrDefault(path);
    }

    /// <summary>Gets the children of a node, or the source's root nodes.</summary>
    /// <param name="parent">The parent's path, or null for the root nodes.</param>
    /// <returns>The children, in the report's order.</returns>
    public IEnumerable<StructureNode> ChildrenOf(NodePath? parent)
        => this.Nodes.Where(node => Equals(node.Parent, parent));
}
