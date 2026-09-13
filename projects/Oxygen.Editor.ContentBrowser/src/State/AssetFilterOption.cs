// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>A selectable filter over the shared asset facts.</summary>
/// <param name="label">The user-facing filter name.</param>
/// <param name="matches">The typed status or kind predicate.</param>
public sealed partial class AssetFilterOption(string label, Func<ContentBrowserAssetItem, bool> matches) : ObservableObject
{
    /// <summary>Gets the user-facing filter name.</summary>
    public string Label { get; } = label;

    /// <summary>Gets or sets a value indicating whether this filter participates in the current query.</summary>
    [ObservableProperty]
    public partial bool IsSelected { get; set; }

    /// <summary>Tests the shared asset facts against this option.</summary>
    /// <param name="asset">The asset to examine.</param>
    /// <returns>Whether this option includes the asset.</returns>
    internal bool Matches(ContentBrowserAssetItem asset) => matches(asset);
}
