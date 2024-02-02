// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Config;

using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.TreeView;

public class RoutesViewModel : TreeViewModelBase
{
    public RoutesViewModel(Routes config)
    {
        var configRoot = new Route()
        {
            Path = RouteAdapter.RootPath,
            Children = config,
        };

        this.Root = new RouteAdapter(configRoot)
        {
            Level = 0,
        };
    }

    public override string? ToString() => nameof(RoutesViewModel);
}
