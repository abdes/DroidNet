// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents the context in which a navigation is taking place, which includes the navigation
/// <see cref="NavigationTargetKey" />, and the <see cref="IRouterState">router state</see>.
/// <remarks>
/// The navigation context is created by a <see cref="IContextProvider">context factory</see>, which
/// implements the <see cref="IContextProvider" /> interface. It is common that the application
/// needs additional data and services available in the navigation context, and implements an
/// extended <see cref="INavigationContext" /> for such purpose.
/// </remarks>
/// </summary>
public interface INavigationContext
{
    /// <summary>
    /// Gets the key of the navigation target where the root content should be loaded.
    /// </summary>
    /// <remarks>
    /// The target key defines how the navigation target is obtained from the IoC Container and is
    /// either one of the special key values (<see cref="Target.Main" />, <see cref="Target.Self" />
    /// or the key used to register a specific the target with the
    /// IoC container as keyed service.
    /// </remarks>
    Target NavigationTargetKey { get; }

    /// <summary>
    /// Gets an opaque reference to the actual navigation target. The interpretation of this target
    /// object is domain specific. For example in a windowed navigation, this could refer to the
    /// corresponding Window object.
    /// </summary>
    object NavigationTarget { get; }

    /// <summary>
    /// Gets the <see cref="IRouterState">router state</see> for this navigation context.
    /// </summary>
    public IRouterState? State { get; }
}
