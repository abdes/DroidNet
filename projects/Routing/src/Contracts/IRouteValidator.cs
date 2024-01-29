// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Contracts;

/// <summary>
/// Defines the interface for route validation logic within the context of an
/// existing routes configuration.
/// </summary>
public interface IRouteValidator
{
    /// <summary>
    /// Validates the specified <paramref name="route" /> within the given
    /// <paramref name="routes" /> configuration.
    /// </summary>
    /// <param name="routes">The routes configuration object.</param>
    /// <param name="route">
    /// The route being added to the configuration and which needs to be
    /// validated.
    /// </param>
    /// <exception cref="RoutesConfigurationException">
    /// Thrown when validation of the <paramref name="route" /> fails.
    /// </exception>
    void ValidateRoute(Routes routes, Route route);
}
