// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
/// One logical asset of the project as every editor surface sees it: its identity, kind, states and cook facts.
/// </summary>
public sealed record AssetEntry(
    AssetPath Identity,
    string DisplayName,
    AssetKind Kind,
    AssetState PrimaryState,
    AssetState? DerivedState,
    AssetRuntimeAvailability RuntimeAvailability,
    string DisplayPath,
    FileSystemPath? SourcePath,
    FileSystemPath? DescriptorPath,
    FileSystemPath? CookedPath,
    string? AssetGuid,
    IReadOnlyList<string> DiagnosticCodes,
    bool IsSelectable)
{
    /// <summary>Gets the retained model that owns this source row or project-produced output.</summary>
    public AssetPath? ImportSource { get; init; }

    /// <summary>Gets the retained model file for information and source navigation, not descriptor editing.</summary>
    public FileSystemPath? ImportSourcePath { get; init; }

    /// <summary>Gets a value indicating whether explicit reimport can target the retained model.</summary>
    public bool CanReimport => this.ImportSource is not null;

    /// <summary>Gets the indexed native identity and physical origin of the cooked representation.</summary>
    public CookedAssetMetadata? CookedMetadata { get; init; }

    /// <summary>Gets the indexed sources masked by the resolved native asset.</summary>
    public IReadOnlyList<CookedAssetMetadata> OverriddenCookedSources { get; init; } = [];

    /// <summary>Gets the effective source when this row explicitly displays a library's own representation.</summary>
    public CookedAssetMetadata? EffectiveCookedSource { get; init; }

    /// <summary>Gets the source label used to explain a masked library copy.</summary>
    public string? EffectiveCookedSourceName { get; init; }

    /// <summary>Gets a value indicating whether ordinary assignments resolve to a different source than this displayed library copy.</summary>
    public bool IsCookedSourceOverridden => this.CookedMetadata is { } displayed && this.EffectiveCookedSource is { } effective
        && (displayed.AssetKey != effective.AssetKey || displayed.ContainerPath != effective.ContainerPath);

    /// <summary>Gets supplemental information from this snapshot without starting discovery, loading or cooking.</summary>
    public AssetInformation Information => AssetInformation.FromAsset(this);

    /// <summary>Gets the native availability explanation, independently of source and publication status.</summary>
    public string? RuntimeReason { get; init; }

    /// <summary>Gets a value indicating whether the asset is inside a pak, such as the engine content pak: read-only content cooks reference without copying.</summary>
    public bool IsPackaged => this.PrimaryState == AssetState.Packaged;

    /// <summary>
    /// Gets a value indicating whether the asset exists only as loose cooked content: it has no authored source or
    /// descriptor and is not in a pak, such as a library asset.
    /// </summary>
    public bool IsCookedOnly => this.SourcePath is null && this.DescriptorPath is null && !this.IsPackaged
        && this.PrimaryState is not AssetState.Missing;

    /// <summary>Gets a value indicating whether this selection owns an authored cook input.</summary>
    public bool CanCook => (this.DescriptorPath is not null || this.ImportSource is not null) && this.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene or AssetKind.Texture;

    /// <summary>Gets the shared saved-input and publication facts, independently of runtime availability.</summary>
    public AssetCookStatus? CookStatus { get; init; }

    /// <summary>Gets the applicable queued, active or most recent cook.</summary>
    public AssetCookActivity? CookActivity { get; init; }

    /// <summary>Gets the user-facing asset type.</summary>
    public string TypeDisplayName => this.Kind switch
    {
        AssetKind.ForeignSource => Path.GetExtension(this.Identity.Name).ToUpperInvariant() switch { ".GLTF" => "glTF source", ".GLB" => "GLB source", ".FBX" => "FBX source", _ => "Source model" },
        AssetKind.CookedData => "Cooked Data",
        AssetKind.CookedTable => "Cooked Table",
        AssetKind.InputFile => "Input file",
        _ => this.Kind.ToString(),
    };

    /// <summary>Gets the primary asset-state badge.</summary>
    public string PrimaryBadge => this.IsCookedSourceOverridden ? "Overridden"
        : this.IsPackaged ? this.PackagedBadge : this.Kind == AssetKind.ForeignSource && this.ImportSource is null ? "Not imported"
        : AssetStatusText.GetText(this.CookStatus, this.CookActivity, runtimeAvailability: this.RuntimeAvailability) ?? GetBadge(this.PrimaryState);

    /// <summary>Gets a concise explanation of engine ownership or the current authored status.</summary>
    public string PrimaryBadgeTooltip => this.IsCookedSourceOverridden
        ? "Scene assignments use " + this.EffectiveCookedSourceName + " instead of this copy."
        : this.IsPackaged
        ? $"Packaged in {this.CookedMetadata?.ContainerPath.Name}: read-only, and cooks reference it without copying it."
            + (this.RuntimeReason is null ? string.Empty : " Preview: " + this.RuntimeReason)
        : this.Kind == AssetKind.ForeignSource && this.ImportSource is null ? "Import this model to create usable geometry, materials and scenes."
        : AssetStatusText.GetDescription(this.CookStatus, this.CookActivity, runtimeAvailability: this.RuntimeAvailability, runtimeReason: this.RuntimeReason);

    /// <summary>
    /// Gets where the asset comes from: the engine content pak (the <c>/Engine</c> mount, even while its pak is
    /// missing), a user pak, or the project's content.
    /// </summary>
    public AssetOrigin Origin => this.Identity.IsSameOrInside(AssetPath.Engine) ? AssetOrigin.Engine
        : this.IsPackaged ? AssetOrigin.Package : AssetOrigin.Content;

    /// <summary>Gets the badge of a packaged asset: Engine for the engine content pak, Pak for any other pak.</summary>
    public string PackagedBadge => this.Origin == AssetOrigin.Engine ? "Engine" : "Pak";

    /// <summary>Gets a value indicating whether the asset has diagnostics.</summary>
    public bool HasDiagnostics => this.DiagnosticCodes.Count > 0;

    /// <summary>Gets the diagnostic codes for the asset.</summary>
    public string DiagnosticsText => string.Join(", ", this.DiagnosticCodes);

    /// <summary>Creates the entry of a referenced asset that no catalog provides.</summary>
    /// <param name="identity">The referenced asset.</param>
    /// <returns>A missing, unselectable entry named after the reference.</returns>
    public static AssetEntry Missing(AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        return new(
            Identity: identity,
            DisplayName: AssetIdentityReducer.GetDisplayName(identity),
            Kind: AssetKinds.FromFileName(identity.Name),
            PrimaryState: AssetState.Missing,
            DerivedState: null,
            RuntimeAvailability: AssetRuntimeAvailability.Unknown,
            DisplayPath: identity.VirtualPath,
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [AssetIdentityDiagnosticCodes.ResolveMissing],
            IsSelectable: false);
    }

    /// <summary>Returns the compact badge for an asset state.</summary>
    /// <param name="state">The asset state.</param>
    /// <returns>The displayed badge.</returns>
    public static string GetBadge(AssetState state)
        => state switch
        {
            AssetState.Packaged => "Packaged",
            AssetState.Source => "SRC",
            AssetState.Descriptor => "DESC",
            AssetState.Cooked => "Cooked",
            AssetState.Stale => "STALE",
            AssetState.Missing => "MISS",
            AssetState.Broken => "ERR",
            _ => "?",
        };
}
