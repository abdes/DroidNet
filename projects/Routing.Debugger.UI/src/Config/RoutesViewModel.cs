// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Config;

using System;
using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// ViewModel for the <see cref="RoutesView" /> view.
/// </summary>
public partial class RoutesViewModel : TreeViewModelBase
{
    public RoutesViewModel(IRoutes config)
    {
        var configRoot = new RootNode(config);
        this.Root = new RouteAdapter(configRoot)
        {
            Level = 0,
        };
    }

    public override string ToString() => nameof(RoutesViewModel);

    private sealed class RootNode(IRoutes config) : IRoute
    {
        public PathMatch MatchMethod => throw new InvalidOperationException();

        public string Path => RouteAdapter.RootPath;

        public IRoute.PathMatcher Matcher => throw new InvalidOperationException();

        public Type? ViewModelType => null;

        public OutletName Outlet => OutletName.Primary;

        public IRoutes Children => config;
    }
}
