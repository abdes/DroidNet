// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests.Tree;

internal sealed partial class TestViewModel(ILoggerFactory? loggerFactory = null) : DynamicTreeViewModel(loggerFactory)
{
    /// <summary>
    /// Loads the tree structure asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public async Task LoadTreeStructureAsync()
    {
        // Create a richer tree structure using the R / Cn / GCn naming convention.
        var rootNode = new TestTreeItem
        {
            Label = "R",
            Children =
            [
                new TestTreeItem
                {
                    Label = "R-C1",
                    Children =
                    [
                        new TestTreeItem { Label = "R-C1-GC1", Children = [] },
                        new TestTreeItem { Label = "R-C1-GC2", Children = [] },
                    ],
                },
                new TestTreeItem
                {
                    Label = "R-C2",
                    Children =
                    [
                        new TestTreeItem { Label = "R-C2-GC1", Children = [] },
                    ],
                },
                new TestTreeItem
                {
                    Label = "R-C3",
                    Children =
                    [],
                },
            ],
        };

        var rootItem = new TestItemAdapter(rootNode, isRoot: true, isHidden: false);
        await this.InitializeRootAsync(rootItem, skipRoot: false).ConfigureAwait(false);
    }
}
