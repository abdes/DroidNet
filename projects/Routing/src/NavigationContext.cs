// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <inheritdoc cref="INavigationContext" />
/// <param name="target">
/// The name of the navigation <see cref="Target">target</see> where the root content is or should be loaded.
/// </param>
public class NavigationContext(Target target) : INavigationContext
{
    /// <inheritdoc />
    public Target Target { get; } = target;

    /// <inheritdoc />
    public IRouterState? State { get; internal set; }

    public IRouteActivationObserver? RouteActivationObserver { get; internal set; }
}
