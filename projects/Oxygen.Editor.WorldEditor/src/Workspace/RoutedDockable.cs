// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using DroidNet.Routing;

namespace Oxygen.Editor.WorldEditor.Workspace;

/// <summary>
/// A <see cref="Dockable" /> that can be the target of a route navigated to via an <see cref="IRouter" />.
/// </summary>
internal sealed partial class RoutedDockable : Dockable
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RoutedDockable"/> class.
    /// </summary>
    /// <param name="id">A unique identifier for this <see cref="RoutedDockable"/>.</param>
    /// <param name="route">The active route providing the parameters for this dockable.</param>
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

    /// <summary>
    /// Gets the deferred docking information for this <see cref="RoutedDockable"/>.
    /// </summary>
    /// <value>
    /// A <see cref="DockingInfo"/> structure containing the docking position, anchor ID, and minimized state.
    /// </value>
    internal DockingInfo DeferredDockingInfo { get; }

    /// <summary>
    /// Creates a new instance of the <see cref="RoutedDockable"/> class.
    /// </summary>
    /// <param name="id">A unique identifier for the <see cref="RoutedDockable"/>.</param>
    /// <param name="route">The active route providing the parameters for this dockable.</param>
    /// <returns>A new instance of the <see cref="RoutedDockable"/> class.</returns>
    internal static RoutedDockable New(string id, IActiveRoute route)
        => (RoutedDockable)Factory.CreateDockable(typeof(RoutedDockable), id, route);

    /// <summary>
    /// Contains information abot a dockable's docking position and state.
    /// </summary>
    internal readonly record struct DockingInfo
    {
        /// <summary>
        /// Gets the position where the dockable should be anchored.
        /// </summary>
        public required AnchorPosition Position { get; init; }

        /// <summary>
        /// Gets the identifier of the anchor to which the dockable is attached.
        /// </summary>
        public string? AnchorId { get; init; }

        /// <summary>
        /// Gets a value indicating whether the dockable is minimized.
        /// </summary>
        public bool IsMinimized { get; init; }
    }
}
