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
/// <param name="context">The navigation context containing the activated routes.</param>
public class ActivationStarted(NavigationOptions options, INavigationContext context) : NavigationEvent(options)
{
    /// <summary> Gets the navigation context.
    /// Can be used to obtain the current <see cref="INavigationContext.State">router state</see>
    /// and the <see cref="INavigationContext.NavigationTarget">navigation target</see>.
    /// </summary>
    /// <remarks>
    /// When this event is fired, the <see cref="INavigationContext.State"/> property is guaranteed
    /// not to be null.
    /// </remarks>
    public INavigationContext Context { get; } = context;

    /// <inheritdoc />
    public override string ToString() => $"Routes activation started -> {this.Context.State?.RootNode}";
}
