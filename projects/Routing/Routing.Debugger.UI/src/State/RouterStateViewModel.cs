// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Routing.Debugger.UI.TreeView;
using DroidNet.Routing.Events;

namespace DroidNet.Routing.Debugger.UI.State;

/// <summary>
/// ViewModel for the <see cref="IRouterState" />.
/// </summary>
public partial class RouterStateViewModel : TreeViewModelBase, IDisposable
{
    private readonly IDisposable routerEventsSub;

    private string? url;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="RouterStateViewModel"/> class.
    /// </summary>
    /// <param name="router">The router instance used to subscribe to router events.</param>
    public RouterStateViewModel(IRouter router)
    {
        this.routerEventsSub = router.Events
        .OfType<ActivationStarted>()
        .Select(e => e.Context.State)
        .Subscribe(
            state =>
            {
                Debug.Assert(state is not null, "router state guanrateed not to be null when ActivationStarted event is fired");
                if (string.Equals(state.Url, this.url, StringComparison.Ordinal))
                {
                    return;
                }

                this.url = state.Url;
                this.Root = new RouterStateAdapter(state.RootNode)
                {
                    Level = 0,
                };
            });
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="RouterStateViewModel"/> and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// true to release both managed and unmanaged resources; false to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.routerEventsSub.Dispose();
        }

        this.isDisposed = true;
    }
}
