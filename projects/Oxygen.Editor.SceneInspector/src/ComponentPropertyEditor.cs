// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector;

/// <summary>A section that edits one component kind, or one node property group, on every selected node.</summary>
public abstract class ComponentPropertyEditor : InspectorSection
{
    /// <summary>Shows the section's values for the selected nodes and targets its edits at them.</summary>
    /// <param name="items">The selected nodes, all of which have the section's component.</param>
    public abstract void UpdateValues(ICollection<SceneNode> items);
}
