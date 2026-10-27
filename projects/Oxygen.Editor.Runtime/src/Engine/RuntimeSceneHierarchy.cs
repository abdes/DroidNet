// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World;

// The hierarchy read's result types are one contract: they stay together.
#pragma warning disable SA1402 // File may only contain a single type
#pragma warning disable MA0048 // File name must match type name

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The hierarchy of a scene of the mounted content, with every scene it instances, read from the scene descriptors alone.</summary>
/// <param name="Exists">Whether the virtual path names a mounted, readable scene; otherwise the hierarchy is empty.</param>
/// <param name="SceneKey">The scene's asset key.</param>
/// <param name="Nodes">The scene's own nodes in descriptor order, then each instance's nodes after them.</param>
/// <param name="Issues">The parts that could not be read.</param>
public sealed record RuntimeSceneHierarchy(bool Exists, Guid SceneKey, ImmutableArray<RuntimeSceneHierarchyNode> Nodes, ImmutableArray<RuntimeSceneHierarchyIssue> Issues);

/// <summary>A node of a scene's hierarchy: one of the scene's own nodes, or one an instance of it brings in.</summary>
/// <param name="Path">The node IDs from the scene read down to the node, shallowest first.</param>
/// <param name="Name">The node's name.</param>
/// <param name="Parent">The parent's index in the hierarchy's nodes, or <see langword="null"/> for a root of the scene read.</param>
/// <param name="SceneKey">The asset key of the scene that defines the node.</param>
/// <param name="InstanceKey">The asset key of the scene the node instances, when it is an instance root.</param>
/// <param name="GeometryPath">
/// The virtual path of the geometry the node renders: <see langword="null"/> when it renders nothing, empty when no
/// mounted source provides it.
/// </param>
/// <param name="HasCamera">Whether the node has a camera, perspective or orthographic.</param>
public sealed record RuntimeSceneHierarchyNode(NodePath Path, string Name, int? Parent, Guid SceneKey, Guid? InstanceKey, string? GeometryPath, bool HasCamera);

/// <summary>A part of a scene's hierarchy that could not be read.</summary>
/// <param name="Code">Why it could not be read.</param>
/// <param name="Node">The instance root's index in the hierarchy's nodes, when the issue is on one.</param>
/// <param name="SceneKey">The scene the issue names.</param>
public sealed record RuntimeSceneHierarchyIssue(RuntimeSceneHierarchyIssueCode Code, int? Node, Guid SceneKey);

/// <summary>Why part of a scene's hierarchy could not be read.</summary>
public enum RuntimeSceneHierarchyIssueCode
{
    /// <summary>An instanced scene is not in the mounted content, or its descriptor cannot be read.</summary>
    SceneUnavailable,

    /// <summary>An instanced scene instances, directly or not, the scene it is in.</summary>
    Cycle,
}

/// <summary>The correlated outcome and hierarchy of one scene hierarchy read.</summary>
/// <param name="Outcome">The outcome of the read command.</param>
/// <param name="Hierarchy">The hierarchy on success; otherwise null.</param>
public sealed record RuntimeSceneHierarchyObservation(RuntimeCommandResult Outcome, RuntimeSceneHierarchy? Hierarchy);
