// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.UrlTree;

using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.TreeView;
using DroidNet.Routing.Events;

/// <summary>ViewModel for the parsed URL tree.</summary>
public partial class UrlTreeViewModel : TreeViewModelBase, IDisposable
{
    private readonly IDisposable routerEventsSub;

    [ObservableProperty]
    private IParameters queryParams = new Parameters();

    /// <summary>The currently selected item in the URL tree.</summary>
    [ObservableProperty]
    private ITreeItem? selectedItem;

    private string? url;

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlTreeViewModel" /> class.
    /// </summary>
    /// <param name="router">
    /// The router, injected, used to fetch the current URL tree.
    /// </param>
    public UrlTreeViewModel(IRouter router) => this.routerEventsSub = router.Events
        .OfType<ActivationStarted>()
        .Select(e => e.RouterState)
        .Subscribe(
            state =>
            {
                if (string.Equals(state.Url, this.url, StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }

                this.url = state.Url;
                this.Root = new UrlSegmentGroupAdapter(state.UrlTree.Root)
                {
                    IndexInItems = 0,
                    Level = 0,
                    Outlet = OutletName.Primary,
                };
                this.QueryParams = state.UrlTree.QueryParams;
            });

    public void Dispose()
    {
        this.routerEventsSub.Dispose();
        GC.SuppressFinalize(this);
    }
}
