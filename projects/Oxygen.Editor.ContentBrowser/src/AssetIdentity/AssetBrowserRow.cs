// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentBrowser.Materials;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Keeps one asset's visual container stable while its immutable status snapshot changes.</summary>
/// <param name="item">The initial asset snapshot.</param>
public sealed partial class AssetBrowserRow(ContentBrowserAssetItem item) : ObservableObject
{
    private ContentBrowserAssetItem item = item;
    private AssetFileFacts? fileFacts;
    private MaterialPreviewColor? baseColor;
    private bool baseColorRead;

    /// <summary>Gets the current asset snapshot used by display bindings and commands.</summary>
    public ContentBrowserAssetItem Item => this.item;

    /// <summary>Gets the asset type used by layout commands.</summary>
    public AssetKind Kind => this.item.Kind;

    /// <summary>Gets the displayed status.</summary>
    public string StatusText => this.item.PrimaryBadge;

    /// <summary>Gets the semantic tone of the displayed status: Neutral, Caution, Critical or Success.</summary>
    public string StatusTone => AssetStatusPresentation.GetToneForText(this.item.PrimaryBadge);

    /// <summary>Gets the folder that holds the asset.</summary>
    public string Location
    {
        get
        {
            var path = this.item.DisplayPath.Replace('\\', '/');
            var slash = path.LastIndexOf('/');
            return slash <= 0 ? "/" : path[..slash];
        }
    }

    /// <summary>Gets the size and change time of the asset's file, read once per snapshot.</summary>
    public AssetFileFacts FileFacts => this.fileFacts ??= AssetFileFacts.Read(this.item);

    /// <summary>Gets the asset file size for display.</summary>
    public string SizeText => this.FileFacts.SizeText;

    /// <summary>Gets the asset file change time for display.</summary>
    public string ModifiedText => this.FileFacts.ModifiedText;

    /// <summary>Gets a material's base colour for its swatch, or null for other kinds.</summary>
    public MaterialPreviewColor? BaseColor
    {
        get
        {
            if (!this.baseColorRead)
            {
                this.baseColor = MaterialPreviewReader.Read(this.item);
                this.baseColorRead = true;
            }

            return this.baseColor;
        }
    }

    /// <summary>Gets a value indicating whether the row shows a colour swatch instead of its type glyph.</summary>
    public bool HasSwatch => this.BaseColor is not null;

    /// <summary>Gets a value indicating whether the row shows its type glyph.</summary>
    public bool HasGlyph => !this.HasSwatch;

    /// <summary>Updates presentation without replacing the row or its selection identity.</summary>
    /// <param name="replacement">The latest snapshot of this asset.</param>
    internal void Update(ContentBrowserAssetItem replacement)
    {
        if (!string.Equals(this.item.IdentityUri.AbsoluteUri, replacement.IdentityUri.AbsoluteUri, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("A row can only update its own asset identity.", nameof(replacement));
        }

        var previousKind = this.item.Kind;
        if (!this.SetProperty(ref this.item, replacement, nameof(this.Item)))
        {
            return;
        }

        this.fileFacts = null;
        this.baseColorRead = false;
        if (previousKind != replacement.Kind)
        {
            this.OnPropertyChanged(nameof(this.Kind));
        }

        this.OnPropertyChanged(nameof(this.StatusText));
        this.OnPropertyChanged(nameof(this.StatusTone));
        this.OnPropertyChanged(nameof(this.Location));
        this.OnPropertyChanged(nameof(this.FileFacts));
        this.OnPropertyChanged(nameof(this.SizeText));
        this.OnPropertyChanged(nameof(this.ModifiedText));
        this.OnPropertyChanged(nameof(this.BaseColor));
        this.OnPropertyChanged(nameof(this.HasSwatch));
        this.OnPropertyChanged(nameof(this.HasGlyph));
    }
}
