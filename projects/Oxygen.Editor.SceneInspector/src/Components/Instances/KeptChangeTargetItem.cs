// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Nodes;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>A node <see cref="KeptChangeTargetPicker"/> lists: its names, whether it is new, and why it cannot be chosen.</summary>
public sealed class KeptChangeTargetItem
{
    /// <summary>Initializes a new instance of the <see cref="KeptChangeTargetItem"/> class.</summary>
    /// <param name="target">The node.</param>
    public KeptChangeTargetItem(KeptChangeTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        this.Target = target;
    }

    /// <summary>Gets the node.</summary>
    public KeptChangeTarget Target { get; }

    /// <summary>Gets the node's names, such as <c>Body/Door_Front_L</c>.</summary>
    public string Names => this.Target.Names;

    /// <summary>Gets the mark of a node the model's last import reported as new.</summary>
    public string Badge => this.Target.IsNew ? "New" : string.Empty;

    /// <summary>Gets a value indicating whether the node can be chosen.</summary>
    public bool IsEnabled => this.Target.Refusal is null;

    /// <summary>Gets the opacity that dims a node that cannot be chosen.</summary>
    public double Opacity => this.IsEnabled ? 1.0 : 0.5;

    /// <summary>Gets why the node cannot be chosen, or its names.</summary>
    public string ToolTip => this.Target.Refusal ?? this.Target.Names;
}
