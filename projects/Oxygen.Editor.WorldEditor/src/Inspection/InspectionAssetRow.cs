// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.World.Inspection;

/// <summary>A cooked asset in a captured report, with independently verified authoring relationships.</summary>
/// <param name="Root">The physical root report.</param>
/// <param name="Asset">The native index entry.</param>
/// <param name="Origin">Verified source ownership, when available.</param>
public sealed record InspectionAssetRow(CookedRootReport Root, CookedAssetEntry Asset, CookedAssetProvenance? Origin)
{
    /// <summary>Gets the assignment consequence of inspecting an overridden physical copy.</summary>
    public string ResolutionText { get; init; } = string.Empty;

    /// <summary>Gets lower-priority sources for this explicitly inspected asset.</summary>
    public IReadOnlyList<string> OtherSources { get; init; } = [];

    /// <summary>Gets assignment-resolution explanation visibility.</summary>
    public Visibility ResolutionVisibility => this.ResolutionText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets optional source-list visibility.</summary>
    public Visibility OtherSourcesVisibility => this.OtherSources.Count > 0 ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the root-qualified row identity.</summary>
    public string Key => this.Root.Inspection.CookedRoot + "|" + this.Asset.VirtualPath;

    /// <summary>Gets the human-readable asset name.</summary>
    public string Name => InspectionAssetLink.DisplayName(this.Origin?.SourceAssetUri ?? new Uri("asset://" + this.Asset.VirtualPath));

    /// <summary>Gets the asset type and physical mount.</summary>
    public string Subtitle => this.Asset.Kind + " · " + this.Root.Name;

    /// <summary>Gets the existing browser icon type.</summary>
    public AssetKind Kind => this.Asset.Kind switch
    {
        ContentCookAssetKind.Material => AssetKind.Material,
        ContentCookAssetKind.Geometry => AssetKind.Geometry,
        ContentCookAssetKind.Scene => AssetKind.Scene,
        ContentCookAssetKind.Texture => AssetKind.Texture,
        _ => AssetKind.Unknown,
    };

    /// <summary>Gets the full cooked asset reference.</summary>
    public string ReferenceText => "asset://" + this.Asset.VirtualPath;

    /// <summary>Gets the source link or unavailable-source explanation.</summary>
    public string SourceText => this.Origin?.SourceAssetUri.ToString() ?? "Verified source information is unavailable.";

    /// <summary>Gets the source action visibility.</summary>
    public Visibility SourceActionVisibility => this.Origin is null ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets the dependency information heading.</summary>
    public string DependenciesHeading => this.Origin is null ? "Dependencies unavailable" : this.Origin.Dependencies.Count == 0 ? "No dependencies" : "Dependencies";

    /// <summary>Gets explicit navigation targets from the published product's dependency graph.</summary>
    public IReadOnlyList<InspectionAssetLink> Dependencies => this.Origin?.Dependencies.Select(static uri => new InspectionAssetLink(uri)).ToArray() ?? [];
}
