// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents the runtime instance of a navigation route, which interface is specified by
/// <see cref="IActiveRoute"/>. Such route is part of the router state, built from the router
/// configuration and the navigation url. When "activated", the associated ViewModel is loaded into
/// the corresponding outlet to update the visual state of the application.
/// </summary>
/// <remarks>
/// An active route is part of the router state, built from the router configuration and navigation
/// URL. When activated, its view model is loaded into the designated outlet to update the
/// application's visual state. The route provides:
/// <list type="bullet">
///   <item>
///     <description>Route configuration and parameters from URL recognition</description>
///   </item>
///   <item>
///     <description>URL segments and query parameters for the matched route</description>
///   </item>
///   <item>
///     <description>Target outlet designation for content display</description>
///   </item>
///   <item>
///     <description>View model reference after activation by the route activator</description>
///   </item>
/// </list>
/// </remarks>
public interface IActiveRoute : IActiveRouteTreeNode
{
    /// <summary>
    /// Gets the list of URL segments matched by this route.
    /// </summary>
    /// <value>
    /// A list of URL segments, where each element corresponds to a segment matched by this route.
    /// For example if the route is `/users/:id`, then the list will contain two segments `users`
    /// and `:id`.
    /// </value>
    public IReadOnlyList<IUrlSegment> Segments { get; }

    /// <summary>
    /// Gets the parameters for this route.
    /// </summary>
    /// <remarks>
    /// Parameters can be passed either as <see cref="IUrlSegment">url segments</see>, in such case
    /// they are usually referred to as positional parameters, or as segment matrix parameters and
    /// are passed as part of each segment in the route <see cref="Segments"/>.
    /// <para>
    /// Any time a route is matched and an active route is created for that match, the router
    /// derives a new set of parameters: the router takes the positional parameters (e.g., ':id') of
    /// the matched URL segments and the matrix parameters of the last matched URL segment and
    /// combines those.
    /// </para>
    /// </remarks>
    public IParameters Params { get; }

    /// <summary>
    /// Gets the query parameters shared by all routes in this navigation.
    /// </summary>
    /// <value>
    /// Query parameters from the URL (after '?'), shared across all routes in the current
    /// navigation state.
    /// </value>
    public IParameters QueryParams { get; }

    /// <summary>
    /// Gets the outlet where this route's content should be displayed.
    /// </summary>
    /// <value>
    /// The outlet name identifying where the route's view model content will be loaded during
    /// activation. Primary routes use the primary outlet, while auxiliary routes specify their
    /// target outlet.
    /// </value>
    public OutletName Outlet { get; }

    /// <summary>
    /// Gets the view model instance after route activation.
    /// </summary>
    /// <remarks>
    /// Created by the route activator during activation. If the view model implements
    /// <see cref="IRoutingAware"/>, it will receive its route instance through dependency injection.
    /// </remarks>
    /// <value>
    /// The activated view model instance, or null if the route has not been activated.
    /// </value>
    public object? ViewModel { get; }

    /// <summary>
    /// Gets the route configuration that defines this route's behavior.
    /// </summary>
    /// <value>
    /// The route configuration containing the path, view model type, and other settings used to
    /// match and activate this route.
    /// </value>
    public IRoute Config { get; }
}
