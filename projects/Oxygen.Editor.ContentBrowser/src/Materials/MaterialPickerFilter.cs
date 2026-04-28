// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// Filter state for the ED-M05 material picker.
/// </summary>
/// <param name="Scope">The asset query scope.</param>
/// <param name="SearchText">Optional free-text search.</param>
/// <param name="IncludeGenerated">Whether generated materials are shown.</param>
/// <param name="IncludeSource">Whether source descriptors are shown.</param>
/// <param name="IncludeCooked">Whether cooked materials are shown.</param>
/// <param name="IncludeMissing">Whether missing/broken assigned material rows are shown.</param>
public sealed record MaterialPickerFilter(
    AssetQueryScope Scope,
    string? SearchText,
    bool IncludeGenerated,
    bool IncludeSource,
    bool IncludeCooked,
    bool IncludeMissing)
{
    /// <summary>
    /// Gets the default global material picker filter.
    /// </summary>
    public static MaterialPickerFilter Default { get; } = new(
        Scope: AssetQueryScope.All,
        SearchText: null,
        IncludeGenerated: true,
        IncludeSource: true,
        IncludeCooked: true,
        IncludeMissing: false);
}
