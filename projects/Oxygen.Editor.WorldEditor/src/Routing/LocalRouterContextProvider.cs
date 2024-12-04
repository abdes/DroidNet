// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Routing;
using DroidNet.Routing.Events;

namespace Oxygen.Editor.WorldEditor.Routing;

/// <summary>
/// The implementation of <see cref="IContextProvider" /> for the dedicated router used inside the content browser.
/// </summary>
internal sealed partial class LocalRouterContextProvider : IContextProvider<NavigationContext>, IDisposable
{
    private readonly ILocalRouterContext theContext;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="LocalRouterContextProvider"/> class.
    /// </summary>
    /// <param name="context"></param>
    public LocalRouterContextProvider(ILocalRouterContext context)
    {
        if (context is not INotifyPropertyChanged contextNotify)
        {
            throw new ArgumentException("context must implement INotifyPropertyChanged", nameof(context));
        }

        this.theContext = context;
        contextNotify.PropertyChanged += (sender, args)
            =>
        {
            if (args.PropertyName!.Equals(nameof(ILocalRouterContext.LocalRouter), StringComparison.Ordinal))
            {
                this.ContextCreated?.Invoke(this, new ContextEventArgs(this.theContext));
            }
        };
    }

#pragma warning disable CS0067
    /// <inheritdoc/>
    public event EventHandler<ContextEventArgs>? ContextChanged;
#pragma warning restore

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
        => (NavigationContext)this.theContext;

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
