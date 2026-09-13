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

    /// <summary>Gets the engine identity that produced a verified project copy, when this is such a copy.</summary>
    public Uri? BuiltinOriginUri { get; init; }

    /// <summary>Gets the native availability explanation, independently of source and publication status.</summary>
    public string? RuntimeReason { get; init; }

    /// <summary>Gets a value indicating whether this is engine-provided content rather than authored source.</summary>
    public bool IsBuiltin => this.Generated is not null || this.PrimaryState == AssetState.Generated;

    /// <summary>Gets a value indicating whether this selection owns an authored cook input.</summary>
    public bool CanCook => !this.IsBuiltin && this.DescriptorPath is not null && this.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene;

    /// <summary>Gets the shared saved-input and publication facts, independently of runtime availability.</summary>
    public AssetCookStatus? CookStatus { get; init; }

    /// <summary>Gets the applicable queued, active or most recent cook.</summary>
    public AssetCookActivity? CookActivity { get; init; }

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
    public string PrimaryBadge => this.Generated?.IsLastKnown == true ? "Preview unavailable"
        : this.IsBuiltin ? "Built-in" : AssetStatusPresentation.GetText(this.CookStatus, this.CookActivity, runtimeAvailability: this.RuntimeAvailability) ?? GetBadge(this.PrimaryState);

    /// <summary>Gets a concise explanation of engine ownership or the current authored status.</summary>
    public string PrimaryBadgeTooltip => this.IsBuiltin
        ? this.Generated?.IsLastKnown == true ? "Built-in asset from the last-known Oxygen catalog. Preview unavailable."
            : "Built-in asset provided by Oxygen. No separate cooking is needed." + (this.RuntimeReason is null ? string.Empty : " Preview: " + this.RuntimeReason)
        : AssetStatusPresentation.GetDescription(this.CookStatus, this.CookActivity, runtimeAvailability: this.RuntimeAvailability, runtimeReason: this.RuntimeReason);

    /// <summary>Gets the optional cooked-state badge.</summary>
    public string? DerivedBadge => this.IsBuiltin && this.Generated?.IsLastKnown != true
        ? this.RuntimeAvailability switch
        {
            AssetRuntimeAvailability.Unavailable => "Preview unavailable",
            AssetRuntimeAvailability.Updating => "Updating preview",
            AssetRuntimeAvailability.Failed => "Preview issue",
            _ => null,
        }
        : this.CookStatus is null && this.DerivedState is { } state ? GetBadge(state) : null;

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
            AssetState.Generated => "Built-in",
            AssetState.Source => "SRC",
            AssetState.Descriptor => "DESC",
            AssetState.Cooked => "Cooked",
            AssetState.Stale => "STALE",
            AssetState.Missing => "MISS",
            AssetState.Broken => "ERR",
            _ => "?",
        };
}
