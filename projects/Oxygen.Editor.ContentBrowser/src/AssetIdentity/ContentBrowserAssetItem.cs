// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
    public string TypeDisplayName => this.Kind switch
    {
        AssetKind.ForeignSource => "Foreign Source",
        AssetKind.CookedData => "Cooked Data",
        AssetKind.CookedTable => "Cooked Table",
        _ => this.Kind.ToString(),
    };

    public string PrimaryBadge => GetBadge(this.PrimaryState);

    public string? DerivedBadge => this.DerivedState is { } state ? GetBadge(state) : null;

    public bool HasDiagnostics => this.DiagnosticCodes.Count > 0;

    public string DiagnosticsText => string.Join(", ", this.DiagnosticCodes);

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
