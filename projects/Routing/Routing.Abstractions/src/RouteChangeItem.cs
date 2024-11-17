// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a single change operation to be applied to the router state during partial navigation.
/// </summary>
/// <remarks>
/// <para>
/// RouteChangeItems allow precise modifications to specific parts of the router state without
/// requiring a full URL-based navigation. They are particularly useful for dynamic UI updates like
/// adding, removing, or reconfiguring panels in a workspace layout.
/// </para>
/// <para>
/// Here's an example of restructuring a workspace that was initially created with the URL
/// "/workspace/(main:editor//side:explorer)":
/// </para>
/// <code><![CDATA[
/// // Initial layout has main editor and side explorer
/// // Add a bottom panel for output
/// var changes = new List<RouteChangeItem>
/// {
///     new() {
///         ChangeAction = RouteChangeAction.Add,
///         Outlet = "bottom",
///         ViewModelType = typeof(OutputPanelViewModel),
///         Parameters = new Parameters { ["height"] = "200" }
///     },
///     // Update explorer panel size
///     new() {
///         ChangeAction = RouteChangeAction.Update,
///         Outlet = "side",
///         Parameters = new Parameters { ["width"] = "300" }
///     },
/// };
///
/// router.Navigate(changes, new PartialNavigation {
///     RelativeTo = workspaceRoute
/// });
/// ]]></code>
/// </remarks>
public class RouteChangeItem
{
    /// <summary>
    /// Gets the outlet name where this change should be applied.
    /// </summary>
    /// <remarks>
    /// Identifies the target outlet in the routing hierarchy where the change will take effect.
    /// Must match an existing outlet name for Update/Delete actions.
    /// </remarks>
    public required OutletName Outlet { get; init; }

    /// <summary>
    /// Gets the type of change to perform.
    /// </summary>
    /// <remarks>
    /// Specifies whether to add a new route, update an existing one's parameters, or remove a route entirely.
    /// The <see cref="ViewModelType"/> and <see cref="Parameters"/> requirements depend on this action.
    /// </remarks>
    public required RouteChangeAction ChangeAction { get; init; }

    /// <summary>
    /// Gets the parameters for the route.
    /// </summary>
    /// <remarks>
    /// Required for Add and Update actions. For Add, these become the initial route parameters.
    /// For Update, these replace the existing route's parameters.
    /// </remarks>
    public IParameters? Parameters { get; init; }

    /// <summary>
    /// Gets the view model type for the route.
    /// </summary>
    /// <remarks>
    /// Required only for Add actions to specify which view model should be created for the new route.
    /// Must match a view model type in the router's configuration.
    /// </remarks>
    public Type? ViewModelType { get; init; }
}
