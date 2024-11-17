// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Specifies the different actions that can be performed when modifying the router state during partial navigation.
/// </summary>
/// <remarks>
/// <para>
/// During partial navigation, the router can modify specific parts of its state tree without performing
/// a full URL-based navigation. These actions allow precise control over route updates, additions,
/// and removals within a specific navigation context.
/// </para>
/// <para>
/// For example, in a layout with multiple panels, you might:
/// </para>
/// <list type="bullet">
///   <item>Add a new details panel: <c>RouteChangeAction.Add</c> with outlet "details"</item>
///   <item>Update panel parameters: <c>RouteChangeAction.Update</c> to change view settings</item>
///   <item>Remove a panel: <c>RouteChangeAction.Delete</c> to close auxiliary content</item>
/// </list>
/// </remarks>
public enum RouteChangeAction
{
    /// <summary>
    /// An invalid or uninitialized action value.
    /// </summary>
    /// <remarks>
    /// Used as a default value to detect unset actions. The router will throw an exception if it
    /// encounters this value during state changes.
    /// </remarks>
    None = 0,

    /// <summary>
    /// Adds a new route to the router state.
    /// </summary>
    /// <remarks>
    /// Used when adding new content to the application. For example:
    /// <code><![CDATA[
    /// new RouteChangeItem {
    ///     ChangeAction = RouteChangeAction.Add,
    ///     Outlet = "details",
    ///     ViewModelType = typeof(UserDetailsViewModel),
    ///     Parameters = new Parameters { ["id"] = "123" }
    /// }
    /// ]]></code>
    /// </remarks>
    Add = 1,

    /// <summary>
    /// Updates an existing route's parameters in the router state.
    /// </summary>
    /// <remarks>
    /// Used to modify the parameters of an existing route without changing its structure. For example:
    /// <code><![CDATA[
    /// new RouteChangeItem {
    ///     ChangeAction = RouteChangeAction.Update,
    ///     Outlet = "details",
    ///     Parameters = new Parameters { ["tab"] = "settings" }
    /// }
    /// ]]></code>
    /// </remarks>
    Update = 2,

    /// <summary>
    /// Removes an existing route from the router state.
    /// </summary>
    /// <remarks>
    /// Used to remove content from the application. For example:
    /// <code><![CDATA[
    /// new RouteChangeItem {
    ///     ChangeAction = RouteChangeAction.Delete,
    ///     Outlet = "details"
    /// }
    /// ]]></code>
    /// </remarks>
    Delete = 3,
}
