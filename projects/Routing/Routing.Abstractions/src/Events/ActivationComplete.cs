// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

/// <summary>
/// Represents an event that is raised when route activation has completed successfully.
/// </summary>
/// <remarks>
/// Signals that route activation has finished and the content corresponding to each activated view
/// model has been loaded into its designated outlet.
/// </remarks>
/// <param name="options">The navigation options used during activation.</param>
/// <param name="context">The navigation context containing the activated routes.</param>
public class ActivationComplete(NavigationOptions options, INavigationContext context) : NavigationEvent(options)
{
    /// <summary>
    /// Gets the navigation context.
    /// Can be used to obtain the current <see cref="INavigationContext.State">router state</see>
    /// and the <see cref="INavigationContext.NavigationTarget">navigation target</see>.
    /// </summary>
    public INavigationContext Context { get; } = context;

    /// <inheritdoc />
    public override string ToString() => $"Routes activation complete -> {this.Context.State?.RootNode}";
}
