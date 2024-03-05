// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;

internal sealed class RoutedDockable : Dockable
{
    internal RoutedDockable(string id, IActiveRoute route)
        : base(id)
    {
        this.ViewModel = route.ViewModel;

        // Get the rest of the dockable properties from the route params
        var routeParams = route.Params;

        this.PreferredWidth = routeParams.WidthOrDefault();
        this.PreferredHeight = routeParams.HeightOrDefault();

        var (anchorPosition, anchorId) = routeParams.AnchorInfoOrDefault();
        var isMinimized = routeParams.FlagIsSet("minimized");
        this.DeferredDockingInfo = new DockingInfo
        {
            Position = anchorPosition,
            AnchorId = anchorId,
            IsMinimized = isMinimized,
        };
    }

    internal DockingInfo DeferredDockingInfo { get; }

    internal static RoutedDockable New(string id, IActiveRoute route)
        => (RoutedDockable)Factory.CreateDockable(typeof(RoutedDockable), id, route);

    internal readonly record struct DockingInfo
    {
        public required AnchorPosition Position { get; init; }

        public string? AnchorId { get; init; }

        public bool IsMinimized { get; init; }
    }
}
