// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Interface representing an activated route associated with a `ViewModel`
/// loaded in an outlet at a particular moment in time. Also serves as an entry
/// point to traverse the router state tree.
/// </summary>
/// <seealso cref="IActiveRouteTreeNode" />
public interface IActiveRoute : IActiveRouteTreeNode
{
    /// <summary>
    /// Gets the list of URL segments matched by this route.
    /// </summary>
    /// <value>
    /// A list of URL segments, where each element corresponds to a segment
    /// matched by this route. For example if the route is `/users/:id`, then
    /// the list will contain two segments `users` and `:id`.
    /// </value>
    IReadOnlyList<IUrlSegment> UrlSegments { get; }

    /// <summary>Gets the parameters for this route.</summary>
    /// <remarks>
    /// Parameters can be passed either as <see cref="IUrlSegment">url segments</see>,
    /// in such case they are usually referred to as positional parameters, or as
    /// segment matrix parameters are passed as part of each segment in the route
    /// <see cref="UrlSegments" />.
    /// <para>
    /// Any time a route is matched and an active route is created for that
    /// match, the router derives a new set of parameters : the router takes
    /// the positional parameters (e.g., ‘:id’) of the matched URL segments and
    /// the matrix parameters of the last matched URL segment and combines
    /// those.
    /// </para>
    /// <para>
    /// You can access parameters outside the segments forming this route by
    /// traversing the router state tree.
    /// </para>
    /// </remarks>
    /// <value>
    /// A read-only dictionary of the combined parameters in this route.
    /// </value>
    IParameters Params { get; }

    /// <summary>
    /// Gets the query parameters. Query parameters are shared by all routes in
    /// the router state tree and are passed after the '?' character in the
    /// navigation URL.
    /// </summary>
    /// <value>A read-only dictionary of all query parameters.</value>
    IParameters QueryParams { get; }

    /// <summary>
    /// Gets the name of the outlet where the `ViewModel for this
    /// <see cref="IActiveRoute" /> is loaded.
    /// </summary>
    /// <value>
    /// The outlet name where the `ViewModel` for this route is loaded. This
    /// usually corresponds to one of the content outlets specified in the
    /// `View` of this route's <see cref="IActiveRouteTreeNode.Parent" />.
    /// </value>
    OutletName Outlet { get; }

    /// <summary>Gets The ViewModel of the route.</summary>
    /// <value>
    /// The ViewModel of the route; <see langword="null" /> until the route has been
    /// activated.
    /// </value>
    object? ViewModel { get; }

    /// <summary>
    /// Gets the <see cref="IRoute">route</see> configuration used to match this
    /// route.
    /// </summary>
    /// <value>The route configuration used to match this route.</value>
    /// <remarks>
    /// Provided mostly for debugging and troubleshooting.
    /// </remarks>
    IRoute RouteConfig { get; }
}
