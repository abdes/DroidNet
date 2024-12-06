// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Oxygen.Editor.WorldEditor.ContentBrowser;

namespace Oxygen.Editor.WorldEditor.Routing;

/// <summary>
/// The implementation of <see cref="IContextProvider{NavigationContext}"/> for the a local child router.
/// </summary>
[SuppressMessage("Microsoft.Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "This class is instantiated by dependency injection.")]
internal sealed partial class LocalRouterContextProvider : IContextProvider<NavigationContext>, IDisposable
{
    private readonly LocalRouterContext theContext;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="LocalRouterContextProvider"/> class.
    /// </summary>
    /// <param name="context">The local router context.</param>
    public LocalRouterContextProvider(ILocalRouterContext context)
    {
        var localContext = context as LocalRouterContext;
        Debug.Assert(localContext is not null, "expecting a context provided by me");

        this.theContext = localContext;
        this.theContext.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName!.Equals(nameof(ILocalRouterContext.LocalRouter), StringComparison.Ordinal))
            {
                // We consider the context fully created when its LocalRouter is set.
                this.ContextCreated?.Invoke(this, new ContextEventArgs(this.theContext));
            }
        };
    }

#pragma warning disable CS0067 // The event is never used, because the context is always the same.
    /// <inheritdoc/>
    public event EventHandler<ContextEventArgs>? ContextChanged;
#pragma warning restore CS0067

    /// <inheritdoc/>
    public event EventHandler<ContextEventArgs>? ContextCreated;

    /// <inheritdoc/>
    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    /// <inheritdoc/>
    public INavigationContext ContextForTarget(Target target, INavigationContext? currentContext = null)
        => this.theContext;

    /// <inheritdoc/>
    public void ActivateContext(INavigationContext context) => _ = context;

    /// <inheritdoc/>
    public NavigationContext ContextForTarget(Target target, NavigationContext? currentContext = null)
        => this.theContext;

    /// <inheritdoc/>
    public void ActivateContext(NavigationContext context) => _ = context;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.ContextDestroyed?.Invoke(this, new ContextEventArgs(this.theContext));
        this.disposed = true;
    }
}
