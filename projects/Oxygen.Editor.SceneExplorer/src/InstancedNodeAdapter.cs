// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// A row for a node an instance brought in, read from the instance source's structure: dimmed, since the scene does not
/// own it, and refusing every structural command (scene instances 6.2, 8.4).
/// </summary>
public sealed partial class InstancedNodeAdapter : NodeRowAdapter
{
    /// <summary>Initializes a new instance of the <see cref="InstancedNodeAdapter"/> class.</summary>
    /// <param name="instanceRoot">The instance root of the scene the node is below.</param>
    /// <param name="path">The node's path below the instance root.</param>
    /// <param name="name">The node's name in the source.</param>
    public InstancedNodeAdapter(NodeId instanceRoot, NodePath path, string name)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(name);
        this.Reference = new NodeReference(instanceRoot, path);
        this.Name = name;
        this.IsDimmed = true;
    }

    /// <inheritdoc />
    public override NodeReference Reference { get; }

    /// <summary>Gets the node's name in the instance's source.</summary>
    public string Name { get; }

    /// <inheritdoc />
    public override string Label
    {
        get => this.Name;
        set => throw new NotSupportedException("A node an instance brought in keeps its source's name.");
    }

    // The same glyph as a node of the scene; the dimming tells them apart.
    protected override string DefaultIconGlyph => "";
}
