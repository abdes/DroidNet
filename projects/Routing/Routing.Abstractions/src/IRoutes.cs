// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a collection of route configurations that define the navigation structure of an application.
/// </summary>
/// <remarks>
/// <para>
/// Route configurations form a hierarchical structure where each route can have child routes, creating
/// a tree that maps URLs to view models. This interface extends <see cref="IList{IRoute}"/> to provide
/// ordered access to routes while adding routing-specific functionality.
/// </para>
/// <para>
/// The order of routes is significant during URL matching - the router processes routes in sequence
/// and uses the first successful match. For example:
/// </para>
/// <code><![CDATA[
/// new Routes([
///     new Route {
///         Path = "users/:id",
///         ViewModelType = typeof(UserDetailsViewModel)
///     },
///     new Route {
///         Path = "users/new",
///         ViewModelType = typeof(NewUserViewModel)
///     }
/// ])
/// ]]></code>
/// <para>
/// In this configuration, the URL "users/new" would incorrectly match the first route because ":id"
/// matches any segment. Routes should be ordered from most specific to least specific to ensure
/// correct matching.
/// </para>
/// </remarks>
public interface IRoutes : IList<IRoute>
{
    /// <summary>
    /// Returns a view of the routes collection prioritized for a specific outlet.
    /// </summary>
    /// <param name="outlet">The target outlet name.</param>
    /// <returns>
    /// A new collection where routes targeting the specified outlet appear first, followed by
    /// routes for other outlets. Within each group, the original route order is preserved.
    /// </returns>
    /// <remarks>
    /// This method is used during route recognition when processing outlet-specific segments in the
    /// URL. By prioritizing routes that match the target outlet, the router can efficiently match
    /// URL segments against the most relevant routes first.
    /// </remarks>
    IRoutes SortedByMatchingOutlet(OutletName outlet);
}
