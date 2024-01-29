// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Custom exception class to indicate validation failure for a specified route.
/// </summary>
/// <param name="message">The error message describing the validation failure.</param>
/// <param name="route">The Route object that failed validation.</param>
public class RoutesConfigurationException(string message, Route route) : Exception(message)
{
    /// <summary>Gets the affected Route object that failed validation.</summary>
    /// <value>The affected Route object that failed validation.</value>
    public Route FailedRoute { get; } = route;
}
