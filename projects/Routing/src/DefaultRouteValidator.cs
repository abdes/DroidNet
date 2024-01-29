// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using DroidNet.Routing.Contracts;

/// <summary>
/// Default implementation of route validation logic within the context of an
/// existing routes configuration.
/// </summary>
public class DefaultRouteValidator : IRouteValidator
{
    private static readonly Lazy<DefaultRouteValidator> Lazy = new(() => new DefaultRouteValidator());

    /// <summary>
    /// Gets the single instance of the <see cref="DefaultRouteValidator" /> class.
    /// </summary>
    /// <remarks>
    /// The single instance use the <see cref="Lazy{T}" /> pattern, which
    /// guarantees that all its public members are thread safe, and is
    /// therefore suitable for implementing a thread safe singleton.
    /// </remarks>
    /// <value>
    /// The single instance of the <see cref="DefaultRouteValidator" /> class.
    /// </value>
    public static IRouteValidator Instance => Lazy.Value;

    /// <summary>
    /// Validates various pre-conditions on a route before it can be added to
    /// an existing routes configuration.
    /// </summary>
    /// <param name="routes">The collection of registered routes.</param>
    /// <param name="route">The specified route to validate.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown when validation of the <paramref name="route" /> fails.
    /// </exception>
    public void ValidateRoute(Routes routes, Route route)
    {
        if (route.Matcher == Route.DefaultMatcher && route.Path is null)
        {
            throw new RoutesConfigurationException(
                "when using the default matcher, a route must specify a path",
                route);
        }

        if (route.Path != null && route.Path.StartsWith('/'))
        {
            throw new RoutesConfigurationException(
                "the path of a route should not start with '/'",
                route);
        }
    }
}
