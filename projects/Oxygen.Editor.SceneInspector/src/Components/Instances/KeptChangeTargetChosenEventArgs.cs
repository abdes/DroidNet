// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>The node <see cref="KeptChangeTargetPicker"/> chose for a kept change.</summary>
/// <param name="target">The chosen node's path below the instance root.</param>
public sealed class KeptChangeTargetChosenEventArgs(NodePath target) : EventArgs
{
    /// <summary>Gets the chosen node's path below the instance root.</summary>
    public NodePath Target { get; } = target;
}
