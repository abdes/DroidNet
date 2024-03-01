// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Thrown when a <see cref="IRoute">route</see> configuration does not satisfy the requirements.
/// </summary>
/// <param name="message">The error message describing the validation failure.</param>
/// <param name="route">The Route object that failed validation.</param>
public class RoutesConfigurationException(string message, IRoute route) : ApplicationException(message)
{
    /// <summary>Gets the affected Route object that failed validation.</summary>
    /// <value>The affected Route object that failed validation.</value>
    public IRoute FailedRoute { get; } = route;
}
