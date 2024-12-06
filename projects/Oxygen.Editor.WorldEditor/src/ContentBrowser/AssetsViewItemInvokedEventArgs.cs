// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Provides data for the <see cref="AssetsLayoutViewModel.ItemInvoked"/> event.
/// </summary>
public class AssetsViewItemInvokedEventArgs : EventArgs
{
    /// <summary>
    /// Initializes a new instance of the <see cref="AssetsViewItemInvokedEventArgs"/> class.
    /// </summary>
    /// <param name="invokedItem">The item that was invoked.</param>
    public AssetsViewItemInvokedEventArgs(GameAsset invokedItem)
    {
        this.InvokedItem = invokedItem;
    }

    /// <summary>
    /// Gets the item that was invoked.
    /// </summary>
    public GameAsset InvokedItem { get; }
}
