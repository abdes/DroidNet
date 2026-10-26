// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// A Scene Explorer row that shows a node: a node of the scene (<see cref="SceneNodeAdapter"/>) or a node an instance
/// brought in (<see cref="InstancedNodeAdapter"/>). Selection, the Eye and Lock name the row's node by its reference.
/// </summary>
public abstract class NodeRowAdapter : LayoutItemAdapter
{
    /// <summary>Gets the reference of the node the row shows.</summary>
    public abstract NodeReference Reference { get; }
}
