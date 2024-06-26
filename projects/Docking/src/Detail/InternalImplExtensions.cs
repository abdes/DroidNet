// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking.Workspace;

internal static class InternalImplExtensions
{
    internal static Dockable AsDockable(this IDockable dockable)
        => dockable as Dockable ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(Dockable)}`, but got an object of type `{dockable.GetType()}`",
            nameof(dockable));

    internal static Dock AsDock(this IDock dock)
        => dock as Dock ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(Dock)}`, but got an object of type `{dock.GetType()}`",
            nameof(dock));

    internal static LayoutDockGroup AsDockGroup(this IDockGroup group)
        => group as LayoutDockGroup ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(LayoutDockGroup)}`, but got an object of type `{group.GetType()}`",
            nameof(group));
}
