// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;

namespace Oxygen.Editor.ContentExplorer.Panes.Assets;

/// <summary>
///     Provides data for the <see cref="Layouts.AssetsLayoutViewModel.ItemInvoked"/> event.
/// </summary>
/// <param name="invokedItem">The item that was invoked.</param>
public class AssetsViewItemInvokedEventArgs(AssetEntry invokedItem) : EventArgs
{
    /// <summary>
    ///     Gets the item that was invoked.
    /// </summary>
    public AssetEntry InvokedItem { get; } = invokedItem;
}
