// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>One node an instance of the source brings in, as the engine composes it.</summary>
/// <param name="Path">The node's path below the instance root: a node of the source is its ID alone.</param>
/// <param name="Name">The node's name.</param>
/// <param name="Parent">The parent's path, or null for a root node of the source.</param>
/// <param name="DefiningScene">The scene that defines the node.</param>
/// <param name="Components">The kinds of the node's components, in the scene schema's names.</param>
/// <param name="Properties">The value of each changeable property of the node's components.</param>
/// <param name="Geometry">The key of the node's geometry, or null when it has none.</param>
/// <param name="Scripts">The node's script slots.</param>
/// <param name="Instance">The source of the scene the node instances, or null when it is not an instance root.</param>
public sealed record StructureNode(
    NodePath Path,
    string Name,
    NodePath? Parent,
    StructureScene DefiningScene,
    ImmutableArray<string> Components,
    ImmutableArray<StructureProperty> Properties,
    Guid? Geometry,
    ImmutableArray<StructureScript> Scripts,
    StructureScene? Instance);

/// <summary>A scene of the composition: the source itself, or one it instances at any depth.</summary>
/// <param name="Key">The scene's asset key.</param>
/// <param name="VirtualPath">The scene's virtual path, or null when its cooked root does not hold it.</param>
public sealed record StructureScene(Guid Key, string? VirtualPath);

/// <summary>A changeable property's composed value and the scene that supplies it.</summary>
/// <param name="Property">The property.</param>
/// <param name="Part">The part of the component the value belongs to, or null for the component's own property.</param>
/// <param name="Value">The value in the scene schema's units: an enum is its member's wire value, an asset its key.</param>
/// <param name="Supplier">The scene whose value wins: the defining scene, or one that changes it.</param>
public sealed record StructureProperty(PropertyId Property, PartId? Part, JsonElement Value, StructureScene Supplier);

/// <summary>A script slot of an instanced node.</summary>
/// <param name="SlotId">The slot's identity.</param>
/// <param name="Script">The script's asset key.</param>
/// <param name="ExecutionOrder">The slot's execution order.</param>
/// <param name="Parameters">The slot's parameter values.</param>
public sealed record StructureScript(Guid SlotId, Guid Script, int ExecutionOrder, ImmutableArray<StructureParameter> Parameters);

/// <summary>A script parameter's composed value and the scene that supplies it.</summary>
/// <param name="Key">The parameter's name.</param>
/// <param name="Value">The value; a node value names a node of the supplier and the path below it.</param>
/// <param name="Supplier">The scene whose value wins.</param>
public sealed record StructureParameter(string Key, JsonElement Value, StructureScene Supplier);

/// <summary>A change a scene of the composition makes that names no node or property of its instance.</summary>
/// <param name="Supplier">The scene that makes the change.</param>
/// <param name="InstanceRoot">The path of the instance root that holds the change.</param>
/// <param name="Path">The change's node path below that instance root.</param>
/// <param name="Property">The changed property, or null for a script parameter.</param>
/// <param name="Parameter">The changed script parameter, or null for a property.</param>
public sealed record StructureUnresolvedChange(
    StructureScene Supplier,
    NodePath InstanceRoot,
    NodePath Path,
    UnresolvedProperty? Property,
    UnresolvedParameter? Parameter);

/// <summary>A changed property as the engine reports it, which the editor's schema may not know.</summary>
/// <param name="OverrideId">The property's override ID; 0 when the engine knows none.</param>
/// <param name="Key">The property's key, or null when the engine knows none.</param>
/// <param name="Part">The part's identity, or null for a component's own property.</param>
public sealed record UnresolvedProperty(int OverrideId, string? Key, Guid? Part);

/// <summary>A changed script parameter.</summary>
/// <param name="SlotId">The slot's identity.</param>
/// <param name="Key">The parameter's name.</param>
public sealed record UnresolvedParameter(Guid SlotId, string Key);

/// <summary>A problem the engine met composing the source.</summary>
/// <param name="Code">The engine's code, such as <c>scene.instance.missing</c>.</param>
/// <param name="Occurrence">The scene being composed where the problem is.</param>
/// <param name="Node">The path of the node concerned, or null.</param>
/// <param name="Scene">The key of the scene concerned, or null.</param>
public sealed record StructureIssue(string Code, StructureScene Occurrence, NodePath? Node, Guid? Scene);

#pragma warning restore SA1402 // File may only contain a single type
