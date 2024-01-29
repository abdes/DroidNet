// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Contracts;

/// <summary>
/// Specifies the interface of a context provider, used by the router to get
/// the <see cref="RouterContext" /> for s a specific <see cref="Target" />,
/// and to activate contexts once routes have been parsed into the router state
/// and activated.
/// </summary>
public interface IContextProvider
{
    /// <summary>
    /// An event that can be used to get notified when the current context has
    /// changed due to an action outside the router.
    /// </summary>
    public event EventHandler<RouterContext?>? ContextChanged;

    /// <summary>
    /// An event that can be used to get notified when a new context is
    /// created.
    /// </summary>
    public event EventHandler<RouterContext>? ContextCreated;

    /// <summary>
    /// An event that can be used to get notified when a context is destroyed
    /// and should no longer usable.
    /// </summary>
    public event EventHandler<RouterContext>? ContextDestroyed;

    /// <summary>
    /// Get the <see cref="RouterContext" /> for the given <paramref name="target" />.
    /// </summary>
    /// <param name="target">
    /// The target name for which a context is needed.
    /// </param>
    /// <param name="currentContext">
    /// The current context being used by the router, provided for
    /// optimization, but may be <c>null</c>.
    /// </param>
    /// <returns>
    /// A <see cref="RouterContext" /> instance that can be used to navigate for the
    /// given <paramref name="target" />.
    /// </returns>
    RouterContext ContextForTarget(string target, RouterContext? currentContext = null);

    /// <summary>
    /// Called by the router once all routes have been activated to activate
    /// the context.
    /// </summary>
    /// <param name="context">The context to activate.</param>
    void ActivateContext(RouterContext context);
}
