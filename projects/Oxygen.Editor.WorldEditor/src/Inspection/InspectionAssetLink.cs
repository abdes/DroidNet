// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspection;

/// <summary>An explicit navigation target in an inspection report.</summary>
/// <param name="Uri">The retained logical identity.</param>
public sealed record InspectionAssetLink(Uri Uri)
{
    /// <summary>Gets the asset name.</summary>
    public string Name => DisplayName(this.Uri);

    /// <summary>Gets the complete reference for selection/copy and tooltips.</summary>
    public string Path => this.Uri.ToString();

    /// <summary>Formats a source or cooked filename while preserving engine generator names.</summary>
    /// <param name="uri">The asset identity.</param>
    /// <returns>The readable asset name.</returns>
    internal static string DisplayName(Uri uri)
    {
        var name = System.IO.Path.GetFileName(System.Uri.UnescapeDataString(uri.AbsolutePath));
        foreach (var suffix in new[] { ".omat.json", ".ogeo.json", ".oscene.json", ".omat", ".ogeo", ".oscene" })
        {
            if (name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return name[..^suffix.Length];
            }
        }

        return name;
    }
}
