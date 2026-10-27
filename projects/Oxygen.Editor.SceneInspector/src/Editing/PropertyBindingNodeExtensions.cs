// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>Updates Schemas bindings from the Inspector's node selection.</summary>
internal static class PropertyBindingNodeExtensions
{
    /// <summary>Refreshes the binding from the selected nodes.</summary>
    /// <typeparam name="T">The bound value type.</typeparam>
    /// <param name="binding">The binding to refresh.</param>
    /// <param name="nodes">The selected nodes.</param>
    /// <param name="targets">Resolves the edited component of each node.</param>
    public static void UpdateFromModel<T>(this PropertyBinding<T> binding, IReadOnlyList<NodeReference> nodes, Func<NodeReference, object?> targets)
    {
        ArgumentNullException.ThrowIfNull(binding);
        ArgumentNullException.ThrowIfNull(nodes);
        ArgumentNullException.ThrowIfNull(targets);
        binding.UpdateFromModel(nodes.Select(targets));
    }
}
