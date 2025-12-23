// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
///     Provides data for the <see cref="AssetsLayoutViewModel.ItemInvoked"/> event.
/// </summary>
/// <param name="invokedItem">The item that was invoked.</param>
public class AssetsViewItemInvokedEventArgs(GameAsset invokedItem) : EventArgs
{
    /// <summary>
    ///     Gets the item that was invoked.
    /// </summary>
    public GameAsset InvokedItem { get; } = invokedItem;
}
