// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents the context in which a navigation is taking place, which includes the navigation
/// <see cref="NavigationTargetKey"/> and the <see cref="IRouterState">router state</see>.
/// </summary>
/// <remarks>
/// A navigation context is a passive data container created by a <see cref="IContextProvider"/>.
/// It is designed to be extended for specific navigation scenarios - for example, the
/// <c>WindowNavigationContext</c> extends it to track <c>Window</c> instances and their lifecycle.
/// </remarks>
public interface INavigationContext
{
    /// <summary>
    /// Gets the key of the navigation target where the root content should be loaded.
    /// </summary>
    /// <remarks>
    /// The target key defines how the navigation target is obtained from the IoC Container.
    /// Can be a special value (<see cref="Target.Main"/>, <see cref="Target.Self"/>)
    /// or a key registered with the IoC container (e.g., for specific <c>Window</c> instances).
    /// </remarks>
    public Target NavigationTargetKey { get; }

    /// <summary>
    /// Gets an opaque reference to the actual navigation target.
    /// </summary>
    /// <remarks>
    /// In window-based navigation, this could be the <c>Window</c> instance where content will be loaded.
    /// The <c>Window</c>'s lifecycle (creation, activation, closing) affects the context's lifecycle
    /// through the context provider.
    /// </remarks>
    public object NavigationTarget { get; }

    /// <summary>
    /// Gets the <see cref="IRouterState">router state</see> for this navigation context.
    /// </summary>
    /// <remarks>
    /// Contains the current tree of active routes and their view models within this
    /// navigation target (e.g., within a specific <c>Window</c>).
    /// </remarks>
    public IRouterState? State { get; }
}
