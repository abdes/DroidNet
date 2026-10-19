// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.Assets;

/// <summary>Read-only asset information assembled from the current catalog snapshot without loading content.</summary>
/// <param name="Title">The full asset name and type.</param>
/// <param name="Kind">The asset type for its existing icon.</param>
/// <param name="Location">The asset's virtual location.</param>
/// <param name="Description">The applicable source, publication or runtime explanation.</param>
/// <param name="Facts">Known origin and source/output relationships.</param>
public sealed record AssetInformation(string Title, AssetKind Kind, string Location, string Description, IReadOnlyList<AssetInformationFact> Facts)
{
    /// <summary>Creates the compact informational projection; recipe output paths alone are not published outputs.</summary>
    /// <param name="asset">The current logical asset snapshot.</param>
    /// <returns>Information suitable for a tooltip or explicit read-only inspection.</returns>
    public static AssetInformation FromAsset(AssetEntry asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var facts = new List<AssetInformationFact>();
        if (asset.IsPackaged)
        {
            facts.Add(new("Source", "Packaged · read-only"));
        }
        else if (asset.ImportSource is { } origin && origin != asset.Identity)
        {
            facts.Add(new("Imported from", origin.VirtualPath));
        }
        else if ((asset.SourcePath ?? asset.DescriptorPath) is { } source)
        {
            facts.Add(new("Source", source.FullPath));
        }
        else if (asset.IsCookedOnly)
        {
            facts.Add(new("Source", "Unavailable · read-only"));
        }

        if (asset.CookedMetadata is { } cooked)
        {
            facts.Add(new(cooked.IsPackaged ? "Pak" : "Cooked source", cooked.ContainerPath.FullPath));
            if (asset.OverriddenCookedSources.Count > 0)
            {
                facts.Add(new("Overrides", string.Join(Environment.NewLine, asset.OverriddenCookedSources.Select(static source => source.ContainerPath).Distinct())));
            }
        }

        AddOutputFacts(asset, facts);

        return new($"{asset.DisplayName} ({asset.TypeDisplayName})", asset.Kind, asset.DisplayPath, GetDescription(asset), facts);
    }

    private static void AddOutputFacts(AssetEntry asset, List<AssetInformationFact> facts)
    {
        if (asset.Kind == AssetKind.ForeignSource && asset.CookStatus is { Outputs.Length: > 0 } status)
        {
            var imported = status.Outputs.Where(output => output.SourceAssetUri == asset.Identity).DistinctBy(static output => output.CookedAssetUri).ToArray();
            var text = string.Join(Environment.NewLine, imported.Take(3).Select(static output => AssetIdentityReducer.GetDisplayName(output.CookedAssetUri) + " (" + output.Kind + ")"));
            if (imported.Length > 3)
            {
                text += string.Create(CultureInfo.InvariantCulture, $"{Environment.NewLine}+{imported.Length - 3} more outputs");
            }

            if (imported.Length > 0)
            {
                facts.Add(new("Outputs", text));
            }

            return;
        }

        var outputs = (asset.IsCookedOnly ? new[] { asset.Identity } : [])
            .Concat(asset.CookStatus?.Outputs.Where(output => output.SourceAssetUri == asset.Identity).Select(static output => output.CookedAssetUri) ?? [])
            .Distinct()
            .Select(static identity => identity.VirtualPath)
            .ToArray();
        if (outputs.Length > 0)
        {
            var text = string.Join(Environment.NewLine, outputs.Take(3));
            if (outputs.Length > 3)
            {
                text += string.Create(CultureInfo.InvariantCulture, $"{Environment.NewLine}+{outputs.Length - 3} more outputs");
            }

            facts.Add(new("Cooked output", text));
        }
    }

    private static string GetDescription(AssetEntry asset)
    {
        var description = asset.IsPackaged || asset.IsCookedSourceOverridden || asset.CookStatus is not null || asset.Kind == AssetKind.ForeignSource ? asset.PrimaryBadgeTooltip : asset.PrimaryState switch
        {
            AssetState.Missing => "The referenced asset could not be found.",
            AssetState.Broken => "The asset could not be read or validated.",
            AssetState.Cooked => "Cooked content is read-only.",
            AssetState.Source when asset.Kind == AssetKind.ForeignSource => "Import this source to create usable assets.",
            AssetState.Descriptor => "Cooking status is not available yet.",
            _ => "Source file.",
        };
        if (!asset.IsPackaged && asset.CookStatus is null && !string.IsNullOrWhiteSpace(asset.RuntimeReason))
        {
            description = (description + " " + asset.RuntimeReason).Trim();
        }

        return description;
    }
}
