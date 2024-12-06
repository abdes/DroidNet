// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// A base ViewModel for the assets view layout.
/// </summary>
public abstract class AssetsLayoutViewModel : ObservableObject
{
    /// <summary>
    /// Occurs when an item in the assets view is invoked.
    /// </summary>
    public event EventHandler<AssetsViewItemInvokedEventArgs>? ItemInvoked;

    /// <summary>
    /// Invokes the <see cref="ItemInvoked"/> event.
    /// </summary>
    /// <param name="item">The game asset that was invoked.</param>
    protected void OnItemInvoked(GameAsset item) => this.ItemInvoked?.Invoke(this, new AssetsViewItemInvokedEventArgs(item));
}
