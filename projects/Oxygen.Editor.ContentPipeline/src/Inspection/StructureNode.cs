// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Inspection;

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>One node an instance of the source brings in, as the source's scene descriptors define it.</summary>
/// <param name="Path">The node's path below the instance root: a node of the source is its ID alone.</param>
/// <param name="Name">The node's name.</param>
/// <param name="Parent">The parent's path, or null for a root node of the source.</param>
/// <param name="DefiningScene">The scene that defines the node.</param>
/// <param name="Instance">The scene the node instances, or null when it is not an instance root.</param>
public sealed record StructureNode(
    NodePath Path,
    string Name,
    NodePath? Parent,
    StructureScene DefiningScene,
    StructureScene? Instance);

/// <summary>A scene of the structure: the source itself, or one it instances at any depth.</summary>
/// <param name="Key">The scene's asset key.</param>
/// <param name="VirtualPath">The scene's virtual path, or null when only its key is known.</param>
public sealed record StructureScene(Guid Key, string? VirtualPath);

/// <summary>A part of the structure that could not be read.</summary>
/// <param name="Code">The issue's code, such as <c>scene.instance.unavailable</c>.</param>
/// <param name="Occurrence">The scene the issue names.</param>
/// <param name="Node">The path of the instance root concerned, or null.</param>
/// <param name="Scene">The key of the scene concerned, or null.</param>
public sealed record StructureIssue(string Code, StructureScene Occurrence, NodePath? Node, Guid? Scene);

#pragma warning restore SA1402 // File may only contain a single type
