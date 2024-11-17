// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.Debugger.UI.TreeView;

namespace DroidNet.Routing.Debugger.UI.Config;

/// <summary>
/// ViewModel for the <see cref="RoutesView" /> view.
/// </summary>
public partial class RoutesViewModel : TreeViewModelBase
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RoutesViewModel"/> class.
    /// </summary>
    /// <param name="config">The configuration object that defines the routes.</param>
    public RoutesViewModel(IRoutes config)
    {
        var configRoot = new RootNode(config);
        this.Root = new RouteAdapter(configRoot)
        {
            Level = 0,
        };
    }

    /// <inheritdoc/>
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
