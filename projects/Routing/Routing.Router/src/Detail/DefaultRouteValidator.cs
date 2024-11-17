// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

/// <summary>
/// Default implementation of route validation logic within the context of an existing routes configuration.
/// </summary>
/// <remarks>
/// <para>
/// The DefaultRouteValidator ensures route configurations meet the required criteria before being added
/// to the routing system. It implements a thread-safe singleton pattern using lazy initialization, making
/// it suitable for use across multiple threads in the application.
/// </para>
/// <para>
/// The validator enforces several key rules about route configuration. First, when using the default
/// matcher, routes must specify a path - this ensures the router can properly match URLs to routes.
/// Second, route paths must not start with a forward slash, as paths are always considered relative
/// to their parent route in the hierarchy.
/// </para>
/// </remarks>
///
/// <example>
/// <strong>Example Route Configurations</strong>
/// <code><![CDATA[
/// // Valid configurations
/// new Route
/// {
///     Path = "users",
///     ViewModelType = typeof(UsersViewModel)
/// }
///
/// new Route
/// {
///     Path = "users/:id",
///     ViewModelType = typeof(UserDetailsViewModel)
/// }
///
/// // Invalid configurations
/// new Route
/// {
///     // Error: Missing path with default matcher
///     ViewModelType = typeof(UsersViewModel)
/// }
///
/// new Route
/// {
///     // Error: Path starts with '/'
///     Path = "/users",
///     ViewModelType = typeof(UsersViewModel)
/// }
/// ]]></code>
/// </example>
internal sealed class DefaultRouteValidator : IRouteValidator
{
    /// <summary>
    /// Backing field for the lazy-initialized singleton instance.
    /// </summary>
    /// <remarks>
    /// Uses the <see cref="Lazy{T}"/> type to ensure thread-safe initialization of the singleton instance.
    /// </remarks>
    private static readonly Lazy<DefaultRouteValidator> Lazy = new(() => new DefaultRouteValidator());

    /// <summary>
    /// Gets the single instance of the <see cref="DefaultRouteValidator"/> class.
    /// </summary>
    /// <remarks>
    /// The single instance uses the <see cref="Lazy{T}"/> pattern, which guarantees that all its
    /// public members are thread safe, and is therefore suitable for implementing a thread-safe singleton.
    /// </remarks>
    public static IRouteValidator Instance => Lazy.Value;

    /// <summary>
    /// Validates various pre-conditions on a route before it can be added to an existing routes configuration.
    /// </summary>
    /// <param name="routes">The collection of registered routes.</param>
    /// <param name="route">The specified route to validate.</param>
    /// <exception cref="RoutesConfigurationException">
    /// Thrown when using the default matcher without specifying a path, or when the route path
    /// starts with a forward slash The exception includes the failed route instance for diagnostic
    /// purposes.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method performs two key validations: First, it ensures that routes using the default matcher
    /// have a path specified, as this is required for URL matching. Second, it verifies that route paths
    /// do not start with a forward slash, maintaining consistency in the route hierarchy.
    /// </para>
    /// <para>
    /// These validations help catch configuration errors early, before they can cause problems during
    /// route recognition or navigation.
    /// </para>
    /// </remarks>
    public void ValidateRoute(Routes routes, Route route)
    {
        if (route.Matcher == Route.DefaultMatcher && route.Path is null)
        {
            throw new RoutesConfigurationException("when using the default matcher, a route must specify a path")
            {
                FailedRoute = route,
            };
        }

        if (route.Path?.StartsWith('/') == true)
        {
            throw new RoutesConfigurationException("the path of a route should not start with '/'")
            {
                FailedRoute = route,
            };
        }
    }
}
