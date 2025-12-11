// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests.Tree;

internal sealed class TestTreeItem
{
    public string Label { get; set; } = string.Empty;

    public IList<TestTreeItem> Children { get; init; } = [];
}
