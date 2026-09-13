// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Discovery;

/// <summary>Engine discovery metadata and its availability, independently of cook recipes and runtime loading.</summary>
/// <param name="Catalog">The current or last-known native catalog, or null when neither is available.</param>
/// <param name="IsLastKnown">Whether browsing is using previously captured metadata.</param>
/// <param name="Notice">The concise availability notice for browser and picker surfaces.</param>
public sealed record BuiltinCatalogSnapshot(BuiltinGeometryCatalog? Catalog, bool IsLastKnown, string? Notice)
{
    /// <summary>Gets a value indicating whether the installed SDK provided this catalog.</summary>
    public bool IsCurrent => this.Catalog is not null && !this.IsLastKnown;
}
