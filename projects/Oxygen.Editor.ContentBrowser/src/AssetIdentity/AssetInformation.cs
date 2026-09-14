// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

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
    public static AssetInformation FromAsset(ContentBrowserAssetItem asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var facts = new List<AssetInformationFact>();
        if (asset.IsBuiltin)
        {
            facts.Add(new("Origin", "Oxygen built-in"));
            if (asset.AliasDescription.Length > 0)
            {
                facts.Add(new("Generator", asset.AliasDescription));
            }
        }
        else if (asset.ImportSourceUri is { } origin && origin != asset.IdentityUri)
        {
            facts.Add(new("Imported from", AssetUriHelper.GetVirtualPath(origin)));
        }
        else if ((asset.SourcePath ?? asset.DescriptorPath) is { } source)
        {
            facts.Add(new("Source", source));
        }
        else if (asset.CookedUri is not null)
        {
            facts.Add(new("Source", "Unavailable · read-only"));
        }

        if (asset.CookedMetadata is { } cooked)
        {
            facts.Add(new("Cooked source", cooked.RootFolderPath));
            if (asset.OverriddenCookedSources.Count > 0)
            {
                facts.Add(new("Overrides", string.Join(Environment.NewLine, asset.OverriddenCookedSources.Select(static source => source.RootFolderPath).Distinct(StringComparer.OrdinalIgnoreCase))));
            }
        }

        AddOutputFacts(asset, facts);

        return new($"{asset.DisplayName} ({asset.TypeDisplayName})", asset.Kind, asset.DisplayPath, GetDescription(asset), facts);
    }

    private static void AddOutputFacts(ContentBrowserAssetItem asset, List<AssetInformationFact> facts)
    {
        if (asset.Kind == AssetKind.ForeignSource && asset.CookStatus is { Outputs.Length: > 0 } status)
        {
            var imported = status.Outputs.Where(output => output.SourceAssetUri == asset.IdentityUri).DistinctBy(static output => output.CookedAssetUri).ToArray();
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

        var outputs = asset.CookedCompanions.Prepend(asset)
            .Select(static item => item.CookedUri)
            .OfType<Uri>()
            .Concat(asset.CookStatus?.Outputs.Where(output => output.SourceAssetUri == asset.IdentityUri).Select(static output => output.CookedAssetUri) ?? [])
            .Distinct()
            .Select(AssetUriHelper.GetVirtualPath)
            .ToArray();
        if (outputs.Length > 0)
        {
            var text = string.Join(Environment.NewLine, outputs.Take(3));
            if (outputs.Length > 3)
            {
                text += string.Create(CultureInfo.InvariantCulture, $"{Environment.NewLine}+{outputs.Length - 3} more outputs");
            }

            facts.Add(new(asset.IsBuiltin ? "Project copy" : "Cooked output", text));
        }
    }

    private static string GetDescription(ContentBrowserAssetItem asset)
    {
        var description = asset.IsBuiltin || asset.IsCookedSourceOverridden || asset.CookStatus is not null || asset.Kind == AssetKind.ForeignSource ? asset.PrimaryBadgeTooltip : asset.PrimaryState switch
        {
            AssetState.Missing => "The referenced asset could not be found.",
            AssetState.Broken => "The asset could not be read or validated.",
            AssetState.Cooked => "Cooked content is read-only.",
            AssetState.Source when asset.Kind == AssetKind.ForeignSource => "Import this source to create usable assets.",
            AssetState.Descriptor => "Cooking status is not available yet.",
            _ => "Source file.",
        };
        if (!asset.IsBuiltin && asset.CookStatus is null && !string.IsNullOrWhiteSpace(asset.RuntimeReason))
        {
            description = (description + " " + asset.RuntimeReason).Trim();
        }

        return description;
    }
}
