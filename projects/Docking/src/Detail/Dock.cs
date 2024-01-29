// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking;

/// <summary>
/// Represents a Dock , which holds a <see cref="Dockable" />, and must be
/// inside a <see cref="DockGroup" />.
/// </summary>
public partial class Dock : IDock
{
    protected Dock()
    {
    }

    public DockId Id { get; private set; }

    internal IDockGroup? Group { get; set; }

    public override string ToString() => $"{this.Id}";
}
