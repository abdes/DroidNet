// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

using System.ComponentModel;
using DroidNet.Routing;

/// <summary>
/// The implementation of <see cref="IContextProvider" /> for the dedicated router used inside the content browser.
/// </summary>
internal sealed partial class RouterContextProvider : IContextProvider, IDisposable
{
    private readonly ILocalRouterContext theContext;
    private bool disposed;

    public RouterContextProvider(ILocalRouterContext context)
    {
        if (context is not RouterContext)
        {
            throw new ArgumentException("context must extend RouterContext", nameof(context));
        }

        if (context is not INotifyPropertyChanged contextNotify)
        {
            throw new ArgumentException("context must implement INotifyPropertyChanged", nameof(context));
        }

        this.theContext = context;
        contextNotify.PropertyChanged += (sender, args)
            =>
        {
            if (args.PropertyName!.Equals(nameof(ILocalRouterContext.Router), StringComparison.Ordinal))
            {
                this.ContextCreated?.Invoke(this, new ContextEventArgs((RouterContext)this.theContext));
            }
        };
    }

#pragma warning disable CS0067
    public event EventHandler<ContextEventArgs>? ContextChanged;
#pragma warning restore

    public event EventHandler<ContextEventArgs>? ContextCreated;

    public event EventHandler<ContextEventArgs>? ContextDestroyed;

    public RouterContext ContextForTarget(Target target, RouterContext? currentContext = null)
        => (RouterContext)this.theContext;

    public void ActivateContext(RouterContext context) =>
        _ = context;

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.ContextDestroyed?.Invoke(this, new ContextEventArgs((RouterContext)this.theContext));
        this.disposed = true;
        GC.SuppressFinalize(this);
    }
}
