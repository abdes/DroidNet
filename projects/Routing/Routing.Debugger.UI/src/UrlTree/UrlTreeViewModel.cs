// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Debugger.UI.TreeView;
using DroidNet.Routing.Events;

namespace DroidNet.Routing.Debugger.UI.UrlTree;

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
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlTreeViewModel" /> class.
    /// </summary>
    /// <param name="router">
    /// The router, injected, used to fetch the current URL tree.
    /// </param>
    public UrlTreeViewModel(IRouter router)
    {
        this.routerEventsSub = router.Events
        .OfType<ActivationStarted>()
        .Select(e => e.Context.State)
        .Subscribe(
            state =>
            {
                Debug.Assert(state is not null, "router state guanrateed not to be null when ActivationStarted event is fired");
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
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="UrlTreeViewModel"/> and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true"/> to release both managed and unmanaged resources; <see langword="false"/> to release only unmanaged resources.
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
