// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>Provides separate containers so toolbar overflow never changes an item's authored visibility.</summary>
public sealed partial class ToolBarItemsControl : ItemsControl
{
    /// <inheritdoc />
    protected override bool IsItemItsOwnContainerOverride(object item) => false;

    /// <inheritdoc />
    protected override DependencyObject GetContainerForItemOverride() => new ContentPresenter();
}
