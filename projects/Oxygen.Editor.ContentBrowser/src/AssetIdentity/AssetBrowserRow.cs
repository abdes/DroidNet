// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Keeps one asset's visual container stable while its immutable status snapshot changes.</summary>
/// <param name="item">The initial asset snapshot.</param>
public sealed partial class AssetBrowserRow(ContentBrowserAssetItem item) : ObservableObject
{
    private ContentBrowserAssetItem item = item;

    /// <summary>Gets the current asset snapshot used by display bindings and commands.</summary>
    public ContentBrowserAssetItem Item => this.item;

    /// <summary>Gets the asset type used by layout commands.</summary>
    public AssetKind Kind => this.item.Kind;

    /// <summary>Updates presentation without replacing the row or its selection identity.</summary>
    /// <param name="replacement">The latest snapshot of this asset.</param>
    internal void Update(ContentBrowserAssetItem replacement)
    {
        if (!string.Equals(this.item.IdentityUri.AbsoluteUri, replacement.IdentityUri.AbsoluteUri, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("A row can only update its own asset identity.", nameof(replacement));
        }

        var previousKind = this.item.Kind;
        if (this.SetProperty(ref this.item, replacement, nameof(this.Item)) && previousKind != replacement.Kind)
        {
            this.OnPropertyChanged(nameof(this.Kind));
        }
    }
}
