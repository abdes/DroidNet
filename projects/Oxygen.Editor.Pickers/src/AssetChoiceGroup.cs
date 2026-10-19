// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.Assets;

namespace Oxygen.Editor.Pickers;

/// <summary>The choices of one asset origin, under a header.</summary>
public sealed class AssetChoiceGroup
{
    internal AssetChoiceGroup(AssetOrigin origin)
    {
        this.Origin = origin;
        this.Title = origin switch
        {
            AssetOrigin.Engine => "Engine",
            AssetOrigin.Package => "Packages",
            _ => "Content",
        };
    }

    /// <summary>Gets the origin of the group's assets.</summary>
    public AssetOrigin Origin { get; }

    /// <summary>Gets the header the group shows.</summary>
    public string Title { get; }

    /// <summary>Gets the group's rows that match the search, in name order.</summary>
    public ObservableCollection<AssetChoiceRow> Rows { get; } = [];
}
