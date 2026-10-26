// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// The effective values of a node of a mounted scene's composition, read from the scene descriptors alone: every change
/// the scene and the scenes it instances make is applied.
/// </summary>
/// <param name="Exists">Whether the virtual path names a mounted, readable scene with the node; otherwise the values are empty.</param>
/// <param name="Name">The node's name.</param>
/// <param name="GeometryPath">
/// The virtual path of the geometry the node renders: <see langword="null"/> when it renders none, empty when no mounted
/// source provides it.
/// </param>
/// <param name="Properties">Every property the node's records hold, then the geometry values it replaces.</param>
public sealed record RuntimeSceneNodeValues(bool Exists, string Name, string? GeometryPath, ImmutableArray<RuntimeSceneNodeProperty> Properties);

/// <summary>A property of a mounted scene's node and its effective value.</summary>
/// <param name="Id">The property.</param>
/// <param name="PartId">The surface of a surface property; empty otherwise.</param>
/// <param name="LayoutRevision">The geometry layout revision of a surface property; <see langword="null"/> otherwise.</param>
/// <param name="Value">The value, in the scene schema's units; an asset by the virtual path of its cooked asset.</param>
public sealed record RuntimeSceneNodeProperty(RuntimeOverrideId Id, Guid PartId, string? LayoutRevision, RuntimeInstanceValue Value);

/// <summary>The correlated outcome and values of one scene node read.</summary>
/// <param name="Outcome">The outcome of the read command.</param>
/// <param name="Values">The values on success; otherwise null.</param>
public sealed record RuntimeSceneNodeObservation(RuntimeCommandResult Outcome, RuntimeSceneNodeValues? Values);
