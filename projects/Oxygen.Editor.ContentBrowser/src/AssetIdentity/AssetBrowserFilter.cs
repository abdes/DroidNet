// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Filter used by the ED-M06 shared content-browser asset provider.
/// </summary>
public sealed record AssetBrowserFilter(
    string? SearchText,
    IReadOnlySet<AssetKind> Kinds,
    bool IncludeGenerated,
    bool IncludeSource,
    bool IncludeDescriptor,
    bool IncludeCooked,
    bool IncludeStale,
    bool IncludeMounted,
    bool IncludeMissing,
    bool IncludeBroken)
{
    /// <summary>
    /// Gets a filter that includes all authoring/cooked browser rows.
    /// </summary>
    public static AssetBrowserFilter Default { get; } = new(
        SearchText: null,
        Kinds: new HashSet<AssetKind>(),
        IncludeGenerated: true,
        IncludeSource: true,
        IncludeDescriptor: true,
        IncludeCooked: true,
        IncludeStale: true,
        IncludeMounted: false,
        IncludeMissing: false,
        IncludeBroken: true);
}
