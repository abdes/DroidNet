// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.UrlTree;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>ViewModel for the parsed URL tree.</summary>
public partial class UrlTreeViewModel : TreeViewModelBase
{
    /// <summary>The currently selected item in the URL tree.</summary>
    [ObservableProperty]
    private ITreeItem? selectedItem;

    /// <summary>
    /// Initializes a new instance of the <see cref="UrlTreeViewModel" /> class.
    /// </summary>
    /// <param name="router">
    /// The router, injected, used to fetch the current URL tree.
    /// </param>
    public UrlTreeViewModel(IRouter router)
    {
        var urlTree = router.GetCurrentUrlTreeForTarget(Target.Self);
        if (urlTree == null)
        {
            return;
        }

        this.Root = new UrlSegmentGroupAdapter(urlTree.Root)
        {
            IndexInItems = 0,
            Level = 0,
            Outlet = OutletName.Primary,
        };
    }
}
