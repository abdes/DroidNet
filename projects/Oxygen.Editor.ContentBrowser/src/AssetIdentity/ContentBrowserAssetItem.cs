// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Shared row model for Content Browser views and typed picker projections.
/// </summary>
public sealed record ContentBrowserAssetItem(
    Uri IdentityUri,
    string DisplayName,
    AssetKind Kind,
    AssetState PrimaryState,
    AssetState? DerivedState,
    AssetRuntimeAvailability RuntimeAvailability,
    string DisplayPath,
    string? SourcePath,
    string? DescriptorPath,
    Uri? CookedUri,
    string? CookedPath,
    string? AssetGuid,
    IReadOnlyList<string> DiagnosticCodes,
    bool IsSelectable)
{
    /// <summary>Gets the engine-provided recipe and identity mapping for generated assets.</summary>
    public GeneratedAssetMetadata? Generated { get; init; }

    /// <summary>Gets the shared saved-input and publication facts, independently of runtime availability.</summary>
    public AssetCookStatus? CookStatus { get; init; }

    /// <summary>Gets the relationship to another name for the same native generator.</summary>
    public string AliasDescription => this.Generated is { } recipe && !string.Equals(recipe.CanonicalName, this.DisplayName, StringComparison.Ordinal)
        ? $"Alias of {recipe.CanonicalName}" : string.Empty;

    /// <summary>Gets the user-facing asset type.</summary>
    public string TypeDisplayName => this.Kind switch
    {
        AssetKind.ForeignSource => "Foreign Source",
        AssetKind.CookedData => "Cooked Data",
        AssetKind.CookedTable => "Cooked Table",
        _ => this.Kind.ToString(),
    };

    /// <summary>Gets the primary asset-state badge.</summary>
    public string PrimaryBadge => GetBadge(this.PrimaryState);

    /// <summary>Gets the optional cooked-state badge.</summary>
    public string? DerivedBadge => this.DerivedState is { } state ? GetBadge(state) : null;

    /// <summary>Gets a value indicating whether the asset has diagnostics.</summary>
    public bool HasDiagnostics => this.DiagnosticCodes.Count > 0;

    /// <summary>Gets the diagnostic codes for the asset.</summary>
    public string DiagnosticsText => string.Join(", ", this.DiagnosticCodes);

    /// <summary>Returns the compact badge for an asset state.</summary>
    /// <param name="state">The asset state.</param>
    /// <returns>The displayed badge.</returns>
    public static string GetBadge(AssetState state)
        => state switch
        {
            AssetState.Generated => "GEN",
            AssetState.Source => "SRC",
            AssetState.Descriptor => "DESC",
            AssetState.Cooked => "COOK",
            AssetState.Stale => "STALE",
            AssetState.Missing => "MISS",
            AssetState.Broken => "ERR",
            _ => "?",
        };
}
