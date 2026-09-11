// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Groups recovery issues under their affected asset.</summary>
public sealed partial class CookingIssueGroup : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="CookingIssueGroup"/> class.</summary>
    /// <param name="key">The source grouping identity.</param>
    /// <param name="name">The asset display name.</param>
    public CookingIssueGroup(string key, string name)
    {
        this.Key = key;
        this.Name = name;
        this.Issues.CollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.Header));
    }

    /// <summary>Gets the source grouping identity.</summary>
    public string Key { get; }

    /// <summary>Gets the affected asset's display name.</summary>
    public string Name { get; }

    /// <summary>Gets an explicit asset-group label and issue count.</summary>
    public string Header => $"Issues · {this.Name} ({this.Issues.Count})";

    /// <summary>Gets the issues affecting this asset.</summary>
    public ObservableCollection<CookingIssueViewModel> Issues { get; } = [];
}
