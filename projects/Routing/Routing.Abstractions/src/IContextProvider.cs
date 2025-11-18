// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.Events;

namespace DroidNet.Routing;

/// <summary>
/// Defines a provider of navigation contexts used by the router during navigation.
/// </summary>
/// <remarks>
/// The context provider is responsible for:
/// <list type="bullet">
///   <item>
///     <description>Creating navigation contexts for specified targets</description>
///   </item>
///   <item>
///     <description>Tracking context lifecycle (creation, changes, destruction)</description>
///   </item>
///   <item>
///     <description>Activating navigation targets when route activation completes</description>
///   </item>
/// </list>
/// <para>
/// During navigation, the router obtains a context for the target, updates it with state
/// and routing data, then passes it to the <see cref="IRouteActivator"/> for route activation.
/// Once all routes are activated, the provider activates the context's
/// <see cref="INavigationContext.NavigationTargetKey">target</see>.
/// </para>
/// <para>
/// As an analogy, consider a web browser with multiple windows and tabs. The context provider
/// manages windows as primary navigation targets and tabs within windows as auxiliary targets.
/// The main window represents the default target, while additional windows and their tabs
/// can be created on demand. When activating a context, the provider ensures the appropriate
/// window and tab become active, similar to bringing a browser window to the front and
/// selecting the correct tab.
/// </para>
/// </remarks>
public interface IContextProvider
{
    /// <summary>
    /// Notifies when the active navigation context changes.
    /// </summary>
    public event EventHandler<ContextEventArgs>? ContextChanged;

    /// <summary>
    /// Notifies when a new navigation context is created.
    /// </summary>
    public event EventHandler<ContextEventArgs>? ContextCreated;

    /// <summary>
    /// Notifies when a navigation context is destroyed.
    /// </summary>
    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    /// <summary>
    /// Gets a navigation context for the specified target.
    /// </summary>
    /// <param name="target">The navigation target requiring a context.</param>
    /// <param name="currentContext">The currently active context, if any.</param>
    /// <param name="options">The navigation options for this navigation, if any. Used to extract target-replacement instructions.</param>
    /// <returns>
    /// A navigation context for the specified target. May return the current context
    /// if appropriate for the target.
    /// </returns>
    public INavigationContext ContextForTarget(Target target, INavigationContext? currentContext = null, NavigationOptions? options = null);

    /// <summary>
    /// Activates the specified navigation context.
    /// </summary>
    /// <param name="context">The navigation context to activate.</param>
    /// <remarks>
    /// Called by the router after all routes are activated to make the navigation target
    /// active in the application.
    /// </remarks>
    public void ActivateContext(INavigationContext context);
}

/// <summary>
/// Defines a generic provider of navigation contexts used by the router during navigation.
/// </summary>
/// <typeparam name="T">The type of the navigation context.</typeparam>
public interface IContextProvider<T> : IContextProvider
    where T : class, INavigationContext
{
    /// <summary>
    /// Gets a navigation context for the specified target.
    /// </summary>
    /// <param name="target">The navigation target requiring a context.</param>
    /// <param name="currentContext">The currently active context, if any.</param>
    /// <param name="options">The navigation options for this navigation, if any. Used to extract target-replacement instructions.</param>
    /// <returns>
    /// A navigation context of type <typeparamref name="T"/> for the specified target. May return
    /// the current context if appropriate for the target.
    /// </returns>
    public T ContextForTarget(Target target, T? currentContext = null, NavigationOptions? options = null);

    /// <summary>
    /// Activates the specified navigation context.
    /// </summary>
    /// <param name="context">The navigation context to activate.</param>
    public void ActivateContext(T context);
}
