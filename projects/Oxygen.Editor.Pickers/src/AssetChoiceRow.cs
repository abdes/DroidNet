// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.Pickers;

/// <summary>A picker row that keeps its identity while its asset's presentation changes.</summary>
/// <param name="choice">The row's first presentation.</param>
public sealed partial class AssetChoiceRow(AssetChoice choice) : ObservableObject
{
    private AssetChoice choice = choice;

    /// <summary>Gets the current presentation of the row's asset.</summary>
    public AssetChoice Choice => this.choice;

    /// <summary>Updates the presentation of the row's asset.</summary>
    /// <param name="replacement">The latest presentation of the same asset.</param>
    /// <exception cref="ArgumentException">The replacement names another asset.</exception>
    internal void Update(AssetChoice replacement)
    {
        if (this.choice.Asset != replacement.Asset)
        {
            throw new ArgumentException("A picker row can only update its own asset.", nameof(replacement));
        }

        _ = this.SetProperty(ref this.choice, replacement, nameof(this.Choice));
    }
}
