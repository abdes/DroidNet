// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Detail;

/// <summary>
/// A list of routes used to configure routing in a desktop application.
/// </summary>
/// <seealso cref="List{T}" />
public class Routes : List<IRoute>, IRoutes
{
    private readonly IRouteValidator routeValidator;

    /// <summary>
    /// Initializes a new instance of the <see cref="Routes" /> class, adding
    /// (after validation) all the elements of the <paramref name="routes" />
    /// collection. The size and capacity of the new list will both be equal to
    /// the size of the given collection.
    /// </summary>
    /// <param name="routes">The initial collection of routes to add.</param>
    /// <param name="routeValidator">
    /// An implementation of the <see cref="IRouteValidator" /> that can be
    /// used to validate routes before they are added. Defaults to
    /// <see cref="DefaultRouteValidator" />.
    /// </param>
    /// <exception cref="RoutesConfigurationException">
    /// When validation for any of the <paramref name="routes" /> being added
    /// fails.
    /// </exception>
    public Routes(IEnumerable<Route> routes, IRouteValidator? routeValidator = null)
    {
        this.routeValidator = routeValidator ?? DefaultRouteValidator.Instance;
        this.AddRange(routes);
    }

    /// <summary>
    /// Sorts the `routes` in such a way that the ones with a
    /// <see cref="Route.Outlet" /> matching <paramref name="outlet" /> come first.
    /// The order of the routes in the original config is otherwise preserved.
    /// </summary>
    /// <param name="outlet">
    /// The outlet name for which routes should be placed first.
    /// </param>
    /// <returns>
    /// A new <see cref="Routes" /> list with the routes which
    /// <see cref="Route.Outlet" /> matches <paramref name="outlet" /> coming first.
    /// </returns>
    /// <remarks>
    /// This is useful when processing <see cref="UrlSegmentGroup" />s with outlet
    /// specific children. We want the routes for the child's outlet to have a chance
    /// to match first.
    /// </remarks>
    public IRoutes SortedByMatchingOutlet(OutletName outlet)
    {
        var sortedRoutes = new Routes([]);

        // Add routes with the specified outlet to the new list
        sortedRoutes.AddRange(this.Where(route => route.Outlet == outlet));

        // Add the remaining routes to the new list
        sortedRoutes.AddRange(this.Where(route => route.Outlet != outlet));

        return sortedRoutes;
    }

    /// <summary>
    /// Validates and, when successful, adds the given <see cref="Route" />
    /// object to the end of the routes list.
    /// </summary>
    /// <param name="route">
    /// The <see cref="Route" /> object to be added.
    /// </param>
    /// <exception cref="RoutesConfigurationException">
    /// When the <paramref name="route" /> validation fails.
    /// </exception>
    /// <seealso cref="ValidateRoute" />
    public void Add(Route route)
    {
        this.ValidateRoute(route);
        base.Add(route);
    }

    /// <summary>
    /// Validates and, when successful, adds each element of the given
    /// <paramref name="routes" /> of <see cref="Route" /> objects, to the end
    /// of the routes list, in the same order they were present in the
    /// collection.
    /// </summary>
    /// <param name="routes">
    /// The collection of <see cref="Route" /> objects to be added.
    /// </param>
    /// <exception cref="RoutesConfigurationException">
    /// When validation for any of the <paramref name="routes" /> being added
    /// fails.
    /// </exception>
    /// <seealso cref="Add" />
    /// <seealso cref="ValidateRoute" />
    public void AddRange(IEnumerable<Route> routes)
    {
        foreach (var route in routes)
        {
            this.Add(route);
        }
    }

    /// <summary>
    /// Validates the given route before adding it to the list. Throws an
    /// exception if validation fails.
    /// </summary>
    /// <param name="route">
    /// The <see cref="Route" /> object to validate.
    /// </param>
    /// <exception cref="RoutesConfigurationException">
    /// When validation of the <paramref name="route" /> fails.
    /// </exception>
    private void ValidateRoute(Route route) => this.routeValidator.ValidateRoute(this, route);
}
