// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Defines an interface for providing navigation contexts.
/// </summary>
/// <remarks>
/// The context provider is used by the router to obtain an <see cref="INavigationContext" /> instance for a navigation
/// target. This context is updated with router state and other routing data as navigation progresses. It is passed to
/// the <see cref="IRouteActivator" /> when a route is activated. After all routes are activated, the router requests
/// the <see cref="IContextProvider" /> to activate the <see cref="INavigationContext.Target">target</see> in the
/// navigation context.
/// </remarks>
public interface IContextProvider
{
    /// <summary>
    /// Occurs when the current context changes due to an external action.
    /// </summary>
    public event EventHandler<ContextEventArgs>? ContextChanged;

    /// <summary>
    /// Occurs when a new context is created.
    /// </summary>
    public event EventHandler<ContextEventArgs>? ContextCreated;

    /// <summary>
    /// Occurs when a context is destroyed and should no longer be used.
    /// </summary>
    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    /// <summary>
    /// Gets the <see cref="INavigationContext" /> for the specified <paramref name="target" />.
    /// </summary>
    /// <param name="target">The target for which a context is needed.</param>
    /// <param name="currentContext">
    /// The current context used by the router, provided for optimization, but may be <see langword="null" />.
    /// </param>
    /// <returns>
    /// A <see cref="INavigationContext" /> instance for the specified <paramref name="target" />.
    /// </returns>
    INavigationContext ContextForTarget(Target target, INavigationContext? currentContext = null);

    /// <summary>
    /// Activates the <see cref="INavigationContext.Target">target</see> in the provided context once all routes have
    /// been activated.
    /// </summary>
    /// <param name="context">The context to activate.</param>
    void ActivateContext(INavigationContext context);
}

public interface IContextProvider<T> : IContextProvider
    where T : class, INavigationContext
{
    T ContextForTarget(Target target, T? currentContext = null);

    void ActivateContext(T context);
}
