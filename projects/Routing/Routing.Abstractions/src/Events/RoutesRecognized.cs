// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when the router successfully matches a URL to route configurations.
/// </summary>
/// <remarks>
/// Signals that URL resolution is complete and the router can proceed with route activation.
/// </remarks>
/// <param name="urlTree">The URL tree that was successfully matched to routes.</param>
public class RoutesRecognized(IUrlTree urlTree) : RouterEvent
{
    /// <summary>
    /// Gets the URL tree that was matched to route configurations.
    /// </summary>
    public IUrlTree UrlTree { get; } = urlTree;

    /// <inheritdoc />
    public override string ToString() => $"Routes recognized -> {this.UrlTree}";
}
