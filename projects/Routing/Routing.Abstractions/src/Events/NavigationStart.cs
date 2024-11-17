// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when navigation begins.
/// </summary>
/// <remarks>
/// Signals that the router is starting URL resolution and route recognition.
/// </remarks>
/// <param name="url">The target URL for navigation, or null if not URL-based.</param>
/// <param name="options">The navigation options to use during navigation.</param>
public class NavigationStart(string? url, NavigationOptions options) : NavigationEvent(options)
{
    /// <summary>
    /// Gets the target URL for this navigation.
    /// </summary>
    public string? Url { get; } = url;

    /// <inheritdoc />
    public override string ToString() => $"Navigation Start -> {this.Url}";
}
