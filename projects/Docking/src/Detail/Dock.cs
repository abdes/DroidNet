// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking;

/// <summary>
/// Represents a Dock , which holds a <see cref="Dockable" />, and must be
/// inside a <see cref="DockGroup" />.
/// </summary>
/// <remarks>
/// Dock is an abstract type. Instances of a concrete `Dock`, can only be
/// created through its <see cref="Factory" />, to ensure that each one of them
/// has a unique ID.
/// <para>
/// The <c>Factory</c> itself is internal use only. However, each concrete
/// `Dock` class will offer a simple and straightforward way to create instances
/// of that specific type. Internally, the implementation will have to use the
/// <c>Factory</c>.
/// <example>
/// <code>
/// var dock = ToolDock.New();
/// </code>
/// </example>
/// </para>
/// </remarks>
public abstract partial class Dock : IDock
{
    protected Dock()
    {
    }

    public DockId Id { get; private set; }

    internal IDockGroup? Group { get; set; }

    public override string ToString() => $"{this.Id}";
}
