// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when the router begins route activation.
/// </summary>
/// <remarks>
/// Signals that the router will create view models for matched routes and load their content into
/// designated outlets. This event occurs after routes are recognized but before any view models are
/// activated.
/// </remarks>
/// <param name="options">The navigation options used during activation.</param>
/// <param name="state">The router state containing the routes that will be activated.</param>
public class ActivationStarted(NavigationOptions options, IRouterState state) : NavigationEvent(options)
{
    /// <summary>
    /// Gets the router state containing the routes that will be activated.
    /// </summary>
    public IRouterState RouterState { get; } = state;

    /// <inheritdoc />
    public override string ToString() => $"Routes activation started -> {this.RouterState.RootNode}";
}
