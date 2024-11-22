// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Routing.Events;
using DryIoc;
using Microsoft.UI.Xaml;

namespace DroidNet.Routing.WinUI;

/// <summary>
/// A router context provider that binds navigation contexts to WinUI window instances.
/// </summary>
/// <param name="container">
/// The IoC container used to resolve window instances. The container must have window types
/// registered with appropriate target keys.
/// </param>
/// <remarks>
/// <para>
/// The window context provider manages the relationship between navigation contexts and WinUI windows
/// in the application. Each context is associated with a specific window instance, which is resolved
/// from the dependency container using the navigation target as a key. This enables sophisticated
/// window-based navigation scenarios where different parts of the application can be displayed in
/// separate windows.
/// </para>
/// <para>
/// Window instantiation behavior is controlled through the dependency container's registration. When
/// registering window types, you can specify them as singletons to reuse the same window instance
/// across multiple navigation requests, or as transient to create new windows for each navigation. This
/// flexibility allows you to implement both single-window and multi-window navigation patterns.
/// </para>
/// <para>
/// Since the default router navigation target (<see cref="Target.Self"/>) uses the currently active
/// window, window activation is directly mapped to context changes. When a window is activated, its
/// associated context becomes the current context, ensuring that navigation operations affect the
/// appropriate window.
/// </para>
/// </remarks>
/// <seealso cref="HostBuilderExtensions.ConfigureRouter"/>
internal sealed class WindowContextProvider(IContainer container) : IContextProvider<NavigationContext>
{
    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextChanged;

    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextCreated;

    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    /// <inheritdoc cref="ContextForTarget(Target, NavigationContext?)"/>
    public INavigationContext ContextForTarget(Target target, INavigationContext? currentContext = null)
        => this.ContextForTarget(target, currentContext as NavigationContext);

    /// <summary>
    /// Creates a navigation context for the specified target.
    /// </summary>
    /// <param name="target">The target to create a context for.</param>
    /// <param name="currentContext">
    /// The current navigation context, if any. Used to optimize context reuse.
    /// </param>
    /// <returns>
    /// A navigation context for the specified target. If <paramref name="currentContext"/> refers
    /// to the same window as would be created for <paramref name="target"/>, returns the current
    /// context instead.
    /// </returns>
    /// <remarks>
    /// <para>
    /// This method resolves a window from the dependency container using the target as a key.
    /// If the resolved window matches the window in the current context, the current context
    /// is reused to maintain state continuity.
    /// </para>
    /// <para>
    /// For new contexts, window activation and closure events are wired up to raise the appropriate
    /// context lifecycle events.
    /// </para>
    /// </remarks>
    public NavigationContext ContextForTarget(Target target, NavigationContext? currentContext = null)
    {
        Debug.Assert(
            currentContext is null or WindowNavigationContext,
            "expecting the router context to have been created by me");

        var window = container.Resolve<Window>(serviceKey: target);

        // If the current context is valid, and we're getting the same Window
        // instance than what we currently have, we just reuse the current
        // context as if the target were "_self".
        if (currentContext != null && (currentContext as WindowNavigationContext)?.Window == window)
        {
            return currentContext;
        }

        var context = new WindowNavigationContext(target, window);
        this.ContextCreated?.Invoke(this, new ContextEventArgs(context));
        window.Activated += (_, args) =>
        {
            Debug.WriteLine(
                $"Window for context '{context}' {args.WindowActivationState} -> notifying subscribers to {nameof(this.ContextChanged)}");
            this.ContextChanged?.Invoke(
                this,
                new ContextEventArgs(args.WindowActivationState == WindowActivationState.Deactivated ? null : context));
        };
        window.Closed += (_, _) => this.ContextDestroyed?.Invoke(this, new ContextEventArgs(context));

        return context;
    }

    /// <inheritdoc cref="ActivateContext(INavigationContext)" />
    public void ActivateContext(NavigationContext context) => this.ActivateContext(context as INavigationContext);

    /// <summary>
    /// Activates a navigation context by bringing its associated window to the foreground.
    /// </summary>
    /// <param name="context">The context to activate.</param>
    /// <remarks>
    /// When a context is activated, its associated window is brought to the foreground using
    /// the WinUI window activation API. This triggers the window's Activated event, which in
    /// turn raises the appropriate context change notifications.
    /// </remarks>
    public void ActivateContext(INavigationContext context)
    {
        Debug.Assert(context is WindowNavigationContext, "expecting the router context to have been created by me");

        if (context is WindowNavigationContext c)
        {
            c.Window.Activate();
        }
    }
}
