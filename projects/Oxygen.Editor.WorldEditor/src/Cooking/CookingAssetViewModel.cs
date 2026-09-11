// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Places an asset's status next to its name and type.</summary>
/// <param name="asset">The identified asset and execution state.</param>
public sealed partial class CookingAssetViewModel(CookRunAsset asset) : ObservableObject
{
    /// <summary>Gets the current asset facts.</summary>
    public CookRunAsset Asset { get; private set; } = asset;

    /// <summary>Gets the logical asset name.</summary>
    public string Name => CookingRunViewModel.GetAssetName(this.Asset.AssetUri.AbsolutePath, this.Asset.AssetUri.AbsoluteUri);

    /// <summary>Gets the asset type.</summary>
    public string Kind => this.Asset.Kind.ToString();

    /// <summary>Gets the asset's concise outcome.</summary>
    public string Status => this.Asset.State == CookAssetState.Unresolved ? "Not completed" : this.Asset.State.ToString();

    /// <summary>Gets a value indicating whether this asset reused existing content.</summary>
    public bool IsReused => this.Asset.State == CookAssetState.Reused;

    /// <summary>Gets the semantic colour category for the observed asset state.</summary>
    public CookRunState IndicatorState => this.Asset.State switch
    {
        CookAssetState.Failed => CookRunState.Failed,
        CookAssetState.Updated or CookAssetState.Reused => CookRunState.Succeeded,
        CookAssetState.Cooking => CookRunState.Cooking,
        CookAssetState.Preparing => CookRunState.Preparing,
        _ => CookRunState.Cancelled,
    };

    /// <summary>Gets the adjacent status glyph.</summary>
    public string StatusGlyph => this.IsReused ? "\uE72C" : this.Asset.State is CookAssetState.Skipped or CookAssetState.Unresolved ? "\uE72A" : CookingRunViewModel.GetGlyph(this.IndicatorState);

    /// <summary>Gets a complete accessible description without relying on colour.</summary>
    public string AccessibleName => $"{this.Name}, {this.Kind}, {this.Status}";

    /// <summary>Updates the observed asset outcome without replacing its row.</summary>
    /// <param name="next">The new asset facts.</param>
    internal void Apply(CookRunAsset next)
    {
        if (this.Asset != next)
        {
            this.Asset = next;
            this.OnPropertyChanged(string.Empty);
        }
    }
}
