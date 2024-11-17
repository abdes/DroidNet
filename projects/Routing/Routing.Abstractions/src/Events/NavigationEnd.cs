// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when navigation completes successfully.
/// </summary>
/// <param name="url">The URL to which navigation completed, or null if not applicable.</param>
/// <param name="options">The navigation options used during navigation.</param>
public class NavigationEnd(string? url, NavigationOptions options) : NavigationEvent(options)
{
    /// <summary>
    /// Gets the URL to which navigation completed.
    /// </summary>
    public string? Url { get; } = url;

    /// <inheritdoc />
    public override string ToString() => $"Navigation End -> {this.Url}";
}
