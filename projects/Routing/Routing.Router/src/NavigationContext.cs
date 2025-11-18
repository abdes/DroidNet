// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Routing;

/// <inheritdoc cref="INavigationContext" />
/// <param name="targetKey">
/// The key identifying the navigation <see cref="NavigationTarget">target</see> where the root content is or
/// should be loaded.
/// </param>
/// <param name="target">The actual navigation target.</param>
/// <param name="fromTarget">The source target window from which this navigation originated, if any.</param>
/// <param name="replaceTarget">Whether the source target should be closed when this context is activated.</param>
public class NavigationContext(Target targetKey, object target, object? fromTarget = null, bool replaceTarget = false) : INavigationContext
{
    /// <inheritdoc />
    public Target NavigationTargetKey { get; } = targetKey;

    /// <inheritdoc />
    public object NavigationTarget { get; } = target;

    /// <summary>
    /// Gets the source target window from which this navigation originated.
    /// </summary>
    /// <value>
    /// The source target object (typically a Window) from which this navigation originated, or null if not applicable.
    /// Used in conjunction with <see cref="ReplaceTarget"/> to close the source window
    /// when navigating to a new window.
    /// </value>
    public object? FromTarget { get; } = fromTarget;

    /// <summary>
    /// Gets a value indicating whether the source target should be closed when this context is activated.
    /// </summary>
    /// <value>
    /// True if the source target should be closed during context activation in a multi-window scenario.
    /// False if no target should be closed. Typically used to close the source window when navigating
    /// to a new window.
    /// </value>
    public bool ReplaceTarget { get; } = replaceTarget;

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
