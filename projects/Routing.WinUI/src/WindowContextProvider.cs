// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

using System.Diagnostics;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

/// <summary>
/// A router context provider (<see cref="IContextProvider" />), that binds
/// contexts to <see cref="Window" /> instances.
/// </summary>
/// <param name="provider">The dependency injector's service provider.</param>
/// <remarks>
/// THe window instantiation behavior is specified when the window type is
/// registered with the Dependency Injector. If the window type is registered
/// as a singleton, the window will be created once, and only its content is
/// changed when we activate the route. To create a new window each time you
/// activate the route, register the window type as a transient.
/// <para>
/// Given that the default router navigation target (<see cref="Target.Self" />)
/// will use the currently active window, windows
/// <see cref="Window.Activated">activation</see> is directly mapped into active
/// <see cref="ContextChanged">context changes</see>.
/// </para>
/// </remarks>
internal sealed class WindowContextProvider(IServiceProvider provider) : IContextProvider
{
    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextChanged;

    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextCreated;

    /// <inheritdoc />
    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    /// <inheritdoc />
    public RouterContext ContextForTarget(Target target, RouterContext? currentContext = null)
    {
        Debug.Assert(
            currentContext is null or WindowRouterContext,
            "expecting the router context to have been created by me");

        var window = provider.GetRequiredKeyedService<Window>(target);

        // If the current context is valid, and we're getting the same Window
        // instance than what we currently have, we just reuse the current
        // context as if the target were "_self".
        if (currentContext != null && (currentContext as WindowRouterContext)?.Window == window)
        {
            return currentContext;
        }

        var context = new WindowRouterContext(target, window);
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

    /// <inheritdoc />
    public void ActivateContext(RouterContext context)
    {
        Debug.Assert(context is WindowRouterContext, "expecting the router context to have been created by me");

        if (context is WindowRouterContext c)
        {
            c.Window.Activate();
        }
    }
}
