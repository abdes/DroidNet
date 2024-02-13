// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.State;

using System.Reactive.Linq;
using DroidNet.Routing.Debugger.UI.TreeView;
using DroidNet.Routing.Events;

/// <summary>ViewModel for the <see cref="IRouterState" />.</summary>
public class RouterStateViewModel : TreeViewModelBase, IDisposable
{
    private readonly IDisposable routerEventsSub;

    public RouterStateViewModel(IRouter router) => this.routerEventsSub = router.Events
        .OfType<ActivationStarted>()
        .Select(e => e.RouterState.Root)
        .Subscribe(
            state => this.Root = new RouterStateAdapter(state)
            {
                Level = 0,
            });

    public void Dispose()
    {
        this.routerEventsSub.Dispose();
        GC.SuppressFinalize(this);
    }
}
