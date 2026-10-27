// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.Controls.PropertyEditors;

/// <summary>
/// One boundary of the LOD policy shown, between LOD <see cref="Index"/> and the next: a Distance threshold, or a
/// Screen-space error pair.
/// </summary>
/// <remarks>Setting a value writes the whole policy through <see cref="LodPolicyEditorModel"/>.</remarks>
public sealed partial class LodTransitionModel : ObservableObject
{
    private readonly LodPolicyEditorModel policy;
    private double first;
    private double second;

    /// <summary>Initializes a new instance of the <see cref="LodTransitionModel"/> class.</summary>
    /// <param name="policy">The rows' policy.</param>
    /// <param name="index">The boundary's index.</param>
    internal LodTransitionModel(LodPolicyEditorModel policy, int index)
    {
        this.policy = policy;
        this.Index = index;
        this.Label = string.Create(CultureInfo.CurrentCulture, $"LOD {index} to {index + 1}");
    }

    /// <summary>Gets the boundary's index: it lies between LOD <c>Index</c> and LOD <c>Index + 1</c>.</summary>
    public int Index { get; }

    /// <summary>Gets the row's label.</summary>
    public string Label { get; }

    /// <summary>Gets the accessible name of the error that enters the finer LOD.</summary>
    public string FinerName => $"{this.Label}, finer above";

    /// <summary>Gets the accessible name of the error that enters the coarser LOD.</summary>
    public string CoarserName => $"{this.Label}, coarser below";

    /// <summary>Gets the unit after the first Distance threshold; the others share it.</summary>
    public string DistanceUnit => this.Index == 0 ? "radii" : string.Empty;

    /// <summary>Gets a value indicating whether the boundary can be edited now.</summary>
    [ObservableProperty]
    public partial bool CanEdit { get; private set; }

    /// <summary>
    /// Gets or sets the Distance threshold: from this view distance on, in radii of the geometry's bounding sphere, the
    /// coarser LOD shows.
    /// </summary>
    public double Distance
    {
        get => this.first;
        set
        {
            if (value != this.first)
            {
                this.policy.WriteDistance(this.Index, value);
            }
        }
    }

    /// <summary>
    /// Gets or sets the screen-space error, the bounding sphere's projected radius in pixels, at or above which the finer
    /// LOD is entered.
    /// </summary>
    public double Finer
    {
        get => this.first;
        set
        {
            if (value != this.first)
            {
                this.policy.WriteFiner(this.Index, value);
            }
        }
    }

    /// <summary>Gets or sets the screen-space error, in pixels, at or below which the coarser LOD is entered.</summary>
    public double Coarser
    {
        get => this.second;
        set
        {
            if (value != this.second)
            {
                this.policy.WriteCoarser(this.Index, value);
            }
        }
    }

    /// <summary>Shows the boundary's values.</summary>
    /// <param name="value">The Distance threshold, or the error that enters the finer LOD.</param>
    /// <param name="coarser">The error that enters the coarser LOD; unused by a Distance policy.</param>
    /// <param name="canEdit">Whether the boundary can be edited now.</param>
    internal void Show(float value, float coarser, bool canEdit)
    {
        this.first = value;
        this.second = coarser;
        this.CanEdit = canEdit;

        // The fields show the policy's values even when an edit was refused after they changed.
        this.OnPropertyChanged(nameof(this.Distance));
        this.OnPropertyChanged(nameof(this.Finer));
        this.OnPropertyChanged(nameof(this.Coarser));
    }
}
