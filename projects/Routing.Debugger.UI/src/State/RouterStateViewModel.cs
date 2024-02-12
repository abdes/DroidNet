// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.State;

using System.Diagnostics;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>ViewModel for the <see cref="IRouterState" />.</summary>
public class RouterStateViewModel : TreeViewModelBase
{
    public RouterStateViewModel(IRouter router)
    {
        var stateRoot = router.GetCurrentStateForTarget(Target.Self);
        Debug.Assert(stateRoot is not null, "stateRoot can not be null in a view model");

        this.Root = new RouterStateAdapter(stateRoot)
        {
            Level = 0,
        };
    }
}
