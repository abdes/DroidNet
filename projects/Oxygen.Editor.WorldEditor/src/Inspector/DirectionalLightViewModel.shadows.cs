// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Owns cascade input applicability without rewriting stored authoring values.</summary>
public sealed partial class DirectionalLightViewModel
{
    /// <summary>Gets a value indicating whether generated distribution affects every selected light.</summary>
    public bool IsDistributionExponentApplicable => this.HasUniformSplitMode(DirectionalCsmSplitMode.Generated)
        && this.MinimumCascadeCount > 1;

    /// <summary>Gets the explanation for an inactive stored distribution exponent.</summary>
    public string DistributionExponentApplicabilityText => this.IsDistributionExponentApplicable ? string.Empty
        : !this.splitModeBinding.HasValue ? "No directional light selected."
        : this.splitModeBinding.IsMixed ? "Stored value; select a common split mode to edit distribution."
        : this.HasUniformSplitMode(DirectionalCsmSplitMode.Generated)
            ? "Stored value; distribution requires more than one cascade on every selected light."
            : "Stored value; used only in Generated mode.";

    /// <summary>Gets a value indicating whether the first manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance1Applicable => this.IsCascadeDistanceApplicable(1);

    /// <summary>Gets a value indicating whether the second manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance2Applicable => this.IsCascadeDistanceApplicable(2);

    /// <summary>Gets a value indicating whether the third manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance3Applicable => this.IsCascadeDistanceApplicable(3);

    /// <summary>Gets a value indicating whether the fourth manual boundary affects every selected light.</summary>
    public bool IsCascadeDistance4Applicable => this.IsCascadeDistanceApplicable(4);

    /// <summary>Gets the explanation for the inactive first stored manual boundary.</summary>
    public string CascadeDistance1ApplicabilityText => this.CascadeDistanceApplicabilityText(1);

    /// <summary>Gets the explanation for the inactive second stored manual boundary.</summary>
    public string CascadeDistance2ApplicabilityText => this.CascadeDistanceApplicabilityText(2);

    /// <summary>Gets the explanation for the inactive third stored manual boundary.</summary>
    public string CascadeDistance3ApplicabilityText => this.CascadeDistanceApplicabilityText(3);

    /// <summary>Gets the explanation for the inactive fourth stored manual boundary.</summary>
    public string CascadeDistance4ApplicabilityText => this.CascadeDistanceApplicabilityText(4);

    private int MinimumCascadeCount => !this.cascadeCountBinding.IsMixed ? this.cascadeCountBinding.Value
        : this.selectedItems?.SelectMany(node => node.Components.OfType<DirectionalLightComponent>())
            .Select(light => light.CascadeCount).DefaultIfEmpty(0).Min() ?? 0;

    private bool HasUniformSplitMode(DirectionalCsmSplitMode mode)
        => this.splitModeBinding.HasValue && !this.splitModeBinding.IsMixed && this.SplitMode == mode;

    private bool IsCascadeDistanceApplicable(int cascade)
        => this.HasUniformSplitMode(DirectionalCsmSplitMode.ManualDistances) && this.MinimumCascadeCount > cascade;

    private string CascadeDistanceApplicabilityText(int cascade)
        => this.IsCascadeDistanceApplicable(cascade) ? string.Empty
            : !this.splitModeBinding.HasValue ? "No directional light selected."
            : this.splitModeBinding.IsMixed ? "Stored manual value; select a common split mode to edit distances."
            : !this.HasUniformSplitMode(DirectionalCsmSplitMode.ManualDistances)
                ? "Stored manual value; not used in Generated mode."
                : "Stored manual value; requires an interior cascade on every selected light. The final cascade uses Maximum distance.";

    private void NotifyCascadeApplicabilityChanged()
    {
        this.OnPropertyChanged(nameof(this.IsDistributionExponentApplicable));
        this.OnPropertyChanged(nameof(this.DistributionExponentApplicabilityText));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance1Applicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance2Applicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance3Applicable));
        this.OnPropertyChanged(nameof(this.IsCascadeDistance4Applicable));
        this.OnPropertyChanged(nameof(this.CascadeDistance1ApplicabilityText));
        this.OnPropertyChanged(nameof(this.CascadeDistance2ApplicabilityText));
        this.OnPropertyChanged(nameof(this.CascadeDistance3ApplicabilityText));
        this.OnPropertyChanged(nameof(this.CascadeDistance4ApplicabilityText));
    }
}
