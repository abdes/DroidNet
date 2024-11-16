// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;

/// <inheritdoc cref="INavigationContext" />
/// <param name="targetKey">
/// The key identifying the navigation <see cref="NavigationTarget">target</see> where the root content is or
/// should be loaded.
/// </param>
/// <param name="target">The actual navigation target.</param>
public class NavigationContext(Target targetKey, object target) : INavigationContext
{
    /// <inheritdoc />
    public Target NavigationTargetKey { get; } = targetKey;

    /// <inheritdoc />
    public object NavigationTarget { get; } = target;

    /// <inheritdoc />
    public IRouterState? State { get; internal set; }

    /// <summary>
    /// Gets the observer for route activation lifecycle events.
    /// </summary>
    [SuppressMessage(
        "ReSharper",
        "VirtualMemberNeverOverridden.Global",
        Justification = "needs to be virtual for mocking")]
    public virtual IRouteActivationObserver? RouteActivationObserver { get; internal set; }
}
