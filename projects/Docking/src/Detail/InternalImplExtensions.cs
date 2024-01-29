// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

internal static class InternalImplExtensions
{
    internal static Dock AsBaseDock(this IDock dock)
        => dock as Dock ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(Dock)}`, but got an object of type `{dock.GetType()}`",
            nameof(dock));

    internal static DockGroup AsDockGroup(this IDockGroup group)
        => group as DockGroup ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(DockGroup)}`, but got an object of type `{group.GetType()}`",
            nameof(group));

    internal static Dockable AsDockable(this IDockable dockable)
        => dockable as Dockable ?? throw new ArgumentException(
            $"expecting an object that I created, i.e. `{typeof(Dockable)}`, but got an object of type `{dockable.GetType()}`",
            nameof(dockable));

    // internal static AbstractDock GetOwner(this IDockable dockable)
    // {
    //     var dockableImpl = dockable.AsDockable();
    //     Debug.Assert(
    //         dockableImpl.Owner is not null,
    //         $"{nameof(IDockable)} with Id `{dockable.Id}` has no owner. Probably not created properly.");
    //     return dockableImpl.Owner;
    // }
}
