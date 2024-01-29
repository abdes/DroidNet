// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.ComponentModel;

public class Docker : IDocker
{
    private readonly RootDockGroup root = new();

    public IDockGroup Root => this.root;

    public void Dock(IDock dock, Anchor anchor)
    {
        _ = this; // For consistency, we don't want this method to be static

        var relativeTo = Detail.Dock.FromId(anchor.DockId) ??
                         throw new ArgumentException($"invalid dock id: {anchor.DockId}", nameof(anchor));
        var group = relativeTo.Group ?? throw new ArgumentException(
            $"dock `{dock}` does not belong to a group and cannot be used as an anchor",
            nameof(anchor));

        group.AsDockGroup().AddDock(dock, anchor);
    }

    public void DockToRoot(IDock dock) => this.root.DockCenter(dock);

    public void DockToRoot(IDock dock, AnchorPosition position)
    {
        var newGroup = new DockGroup();
        newGroup.AddDock(dock);

        switch (position)
        {
            case AnchorPosition.Left:
                this.root.DockLeft(newGroup);
                break;

            case AnchorPosition.Top:
                this.root.DockTop(newGroup);
                break;

            case AnchorPosition.Right:
                this.root.DockRight(newGroup);
                break;

            case AnchorPosition.Bottom:
                this.root.DockBottom(newGroup);
                break;

            default:
                throw new InvalidEnumArgumentException(nameof(position), (int)position, typeof(AnchorPosition));
        }
    }
}
