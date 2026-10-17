// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Where a node is: its place in the graph, its local transform and its entry's place in the layout.</summary>
/// <param name="Node">The node.</param>
/// <param name="At">Its place in the scene graph.</param>
/// <param name="Local">Its local transform; only the position, rotation and scale are used.</param>
/// <param name="Layout">Its entry's place in the explorer layout.</param>
public sealed record NodePlacementState(NodeId Node, NodePosition At, TransformData Local, LayoutPosition Layout);
