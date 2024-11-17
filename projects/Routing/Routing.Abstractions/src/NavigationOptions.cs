// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Base class for options that control router navigation behavior.
/// </summary>
/// <remarks>
/// <para>
/// Navigation options determine how the router processes navigation requests, supporting both full
/// navigation with absolute URLs and partial updates to specific parts of the router state. Full
/// navigation creates an entirely new state tree, while partial navigation allows targeted updates
/// to portions of the existing state, offering fine-grained control over the application's navigation
/// structure.
/// </para>
/// <para>
/// Through a combination of navigation target, relative route context, and custom data, these
/// options provide the router with the information needed to resolve URLs, locate the appropriate
/// navigation context, and maintain proper state during navigation operations. This flexibility
/// enables sophisticated navigation scenarios while maintaining a clean and predictable navigation
/// model.
/// </para>
/// </remarks>
public abstract class NavigationOptions
{
    /// <summary>
    /// Gets the target of the navigation.
    /// </summary>
    /// <value>
    /// A navigation target identifier that determines where content will be loaded. This can be a
    /// predefined value like <see cref="Target.Main"/> or <see cref="Target.Self"/>, or a custom
    /// identifier registered with the dependency injection container. The router uses this value to
    /// locate or create an appropriate navigation context for the operation.
    /// </value>
    public Target? Target { get; init; }

    /// <summary>
    /// Gets the active route relative to which navigation will occur.
    /// </summary>
    /// <value>
    /// When specified, this route provides the context for resolving relative URLs and partial
    /// navigation operations. The router uses this reference point to determine where in the
    /// navigation hierarchy changes should be applied, enabling precise control over state updates.
    /// </value>
    public virtual IActiveRoute? RelativeTo { get; init; }

    /// <summary>
    /// Gets additional data passed through to navigation event callbacks.
    /// </summary>
    /// <value>
    /// Application-specific data that travels with the navigation request, allowing custom
    /// navigation behaviors to be implemented through the router's event system. This data is
    /// preserved throughout the navigation lifecycle and made available to event handlers.
    /// </value>
    public object? AdditionalInfo { get; init; }
}
