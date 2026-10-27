// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>The geometry values one selected node shows, and what the scene changes of them.</summary>
/// <remarks>
/// For a node of the scene both are its component's overrides. For a node an instance brings in, the values hold its
/// source's overrides and this scene's changes over them, while only the changes mark a row and reset (scene instances
/// 8.5): resetting takes the source's value back, which may itself override the geometry asset's.
/// </remarks>
/// <param name="Values">The values the node shows over its geometry asset's.</param>
/// <param name="Changes">What the scene changes of the node's source: the overrides the rows mark and reset.</param>
internal sealed record GeometryOverrides(SourceOverrides Values, SourceOverrides Changes);
