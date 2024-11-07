// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

using System.Diagnostics;
using DryIoc;
using Microsoft.UI.Xaml;

/// <summary>
/// A router context provider (<see cref="IContextProvider" />), that binds contexts to <see cref="Window" /> instances.
/// </summary>
/// <param name="container">
/// The IoC container, used to resolve the <see cref="Window" /> instance for a specific <see cref="Target" />.
/// </param>
/// <remarks>
/// THe window instantiation behavior is specified when the window type is registered with the Dependency Injector. If the window
/// type is registered as a singleton, the window will be created once, and only its content is changed when we activate the
/// route. To create a new window each time you activate the route, register the window type as a transient.
/// <para>
/// Given that the default router navigation target (<see cref="Target.Self" />) will use the currently active window, windows
/// <see cref="Window.Activated">activation</see> is directly mapped into active <see cref="ContextChanged">context changes</see>.
/// </para>
/// </remarks>
internal sealed class WindowContextProvider(IContainer container) : IContextProvider<NavigationContext>
{
    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextChanged;

    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextCreated;

    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    public INavigationContext ContextForTarget(Target target, INavigationContext? currentContext = null)
        => this.ContextForTarget(target, currentContext as NavigationContext);

    /// <inheritdoc />
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

    public void ActivateContext(NavigationContext context) => this.ActivateContext(context as INavigationContext);

    /// <inheritdoc />
    public void ActivateContext(INavigationContext context)
    {
        Debug.Assert(context is WindowNavigationContext, "expecting the router context to have been created by me");

        if (context is WindowNavigationContext c)
        {
            c.Window.Activate();
        }
    }
}
